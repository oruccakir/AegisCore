import { log, Config }       from '../config.js';
import { SerialBridge }      from '../serial/SerialBridge.js';
import { WsServer }          from '../ws/WsServer.js';
import { CmdId, encodeCommand, encodeTelemetry } from '../serial/AC2Framer.js';
import { hmacVerify }        from '../serial/Hmac.js';
import type { AC2Frame }     from '../serial/AC2Parser.js';
import type { InboundCmd }   from '../ws/schemas.js';

const STATE_NAME: Record<number, string> = {
  0: 'idle', 1: 'search', 2: 'track', 3: 'fail_safe',
};

const HB_INTERVAL_MS = 1_000;
const PSK_HEX = Config.pskHex;
const VISION_TIMEOUT_MS = 3_000;

interface InferenceResponse {
  class_id: number;
  class_name: string;
  confidence: number;
}

export class Bridge {
  private serial:    SerialBridge;
  private ws:        WsServer;
  private psk:       Buffer;
  private txSeq      = 0;
  private hbTimer:   ReturnType<typeof setInterval> | null = null;
  private lastVisionTxMs = 0;

  constructor() {
    this.serial = new SerialBridge(Config.serialPort, Config.serialBaud);
    this.ws     = new WsServer(Config.wsHost, Config.wsPort);
    this.psk    = Buffer.from(PSK_HEX, 'hex');
  }

  async start(): Promise<void> {
    this.serial.on('frame', (f: AC2Frame) => this.onSerialFrame(f));
    this.serial.on('error', (e: Error)   => log('error', 'serial error', e.message));

    this.ws.on('command', (cmd: InboundCmd, seq: number) => this.onWsCommand(cmd, seq));

    await this.serial.open();

    this.hbTimer = setInterval(() => this.sendHeartbeat(), HB_INTERVAL_MS);
    log('info', 'Bridge started');
  }

  async stop(): Promise<void> {
    if (this.hbTimer) clearInterval(this.hbTimer);
    await this.serial.close();
    await this.ws.close();
    log('info', 'Bridge stopped');
  }

  // ---- Serial → WebSocket ---------------------------------------------------

  private onSerialFrame(frame: AC2Frame): void {
    if (!frame.crcOk) {
      log('warn', 'CRC error dropped', { seq: frame.seq, crcErrors: this.serial.crcErrors });
      return;
    }

    // Telemetry / heartbeat frames from the edge arrive with zero HMAC — skip verify.
    // Command-response frames from the edge also use zero HMAC by our firmware convention.
    // If the frame has a non-zero HMAC we verify it.
    const hasHmac = frame.hmac.some((b) => b !== 0);
    if (hasHmac && !hmacVerify(this.psk, frame.cmd, frame.payload, frame.hmac)) {
      log('warn', 'HMAC verify failed', { seq: frame.seq, cmd: hex(frame.cmd) });
      return;
    }

    log('debug', 'serial frame', { cmd: hex(frame.cmd), seq: frame.seq, len: frame.payload.length });

    switch (frame.cmd) {
      case CmdId.ReportState:
        this.ws.broadcast({
          type:       'evt.report_state',
          state:      frame.payload[0] ?? 0,
          prev_state: frame.payload[1] ?? 0,
          uptime_ms:  frame.payload.readUInt32LE(2),
        });
        break;

      case CmdId.TelemetryTick:
        this.ws.broadcast({
          type:             'evt.telemetry',
          state:            frame.payload[0] ?? 0,
          cpu_load_x10:     frame.payload.readUInt16LE(1),
          stack_uart_rx:    frame.payload.readUInt16LE(3),
          stack_state_core: frame.payload.readUInt16LE(5),
          stack_tel_tx:     frame.payload.readUInt16LE(7),
          stack_heartbeat:  frame.payload.readUInt16LE(9),
          hb_miss_count:    frame.payload[11] ?? 0,
        });
        break;

      case CmdId.TaskList: {
        const count = frame.payload[0] ?? 0;
        const ENTRY = 14;
        const tasks = [];
        for (let i = 0; i < count; i++) {
          const off = 1 + i * ENTRY;
          if (off + ENTRY > frame.payload.length) break;
          const nameBytes = frame.payload.subarray(off, off + 8);
          const nullIdx = nameBytes.indexOf(0);
          const name = nameBytes.subarray(0, nullIdx < 0 ? 8 : nullIdx).toString('ascii');
          tasks.push({
            name,
            state:           frame.payload[off + 8]  ?? 0,
            priority:        frame.payload[off + 9]  ?? 0,
            stack_watermark: frame.payload.readUInt16LE(off + 10),
            cpu_load:        frame.payload[off + 12] ?? 0,
            task_id:         frame.payload[off + 13] ?? 0,
          });
        }
        this.ws.broadcast({ type: 'evt.task_list', tasks });
        break;
      }

      case CmdId.RangeScanReport:
        if (frame.payload.length >= 6) {
          const flags = frame.payload[3] ?? 0;
          this.ws.broadcast({
            type: 'evt.range_scan',
            angle_deg: frame.payload[0] ?? 0,
            distance_cm: frame.payload.readUInt16LE(1),
            locked: (flags & 0x01) !== 0,
            valid: (flags & 0x02) !== 0,
            threshold_cm: frame.payload.readUInt16LE(4),
          });
        }
        break;

      case CmdId.FaultReport:
        this.ws.broadcast({
          type:              'evt.fault_report',
          fault_code:        frame.payload[0] ?? 0,
          ctx:               frame.payload.readUInt16LE(1),
          reset_reason_bits: frame.payload.readUInt32LE(3),
        });
        break;

      case CmdId.Heartbeat:
        this.ws.broadcast({
          type:      'evt.heartbeat',
          uptime_ms: frame.payload.readUInt32LE(0),
        });
        break;

      case CmdId.VersionReport: {
        const sha = frame.payload.subarray(3, 11).toString('hex');
        this.ws.broadcast({
          type:     'evt.version_report',
          major:    frame.payload[0] ?? 0,
          minor:    frame.payload[1] ?? 0,
          patch:    frame.payload[2] ?? 0,
          git_sha:  sha,
          build_ts: frame.payload.readUInt32LE(11),
        });
        break;
      }

      case CmdId.Ack:
        this.ws.broadcast({ type: 'evt.ack',  echoed_seq: frame.payload.readUInt32LE(0) });
        break;

      case CmdId.Nack:
        this.ws.broadcast({
          type:       'evt.nack',
          echoed_seq: frame.payload.readUInt32LE(0),
          err_code:   frame.payload[4] ?? 0,
        });
        break;

      default:
        log('debug', 'unhandled cmd from edge', { cmd: hex(frame.cmd) });
    }
  }

  // ---- WebSocket → Serial ---------------------------------------------------

  private onWsCommand(cmd: InboundCmd, _seq: number): void {
    log('debug', 'ws command', cmd);

    switch (cmd.type) {
      case 'cmd.manual_lock': {
        const payload = Buffer.alloc(1);
        payload[0] = cmd.lock ? 1 : 0;
        this.serial.write(encodeCommand(CmdId.ManualLock, payload, this.txSeq++, this.psk));
        break;
      }
      case 'cmd.get_version':
        this.serial.write(encodeCommand(CmdId.GetVersion, Buffer.alloc(0), this.txSeq++, this.psk));
        break;

      case 'cmd.heartbeat':
        this.sendHeartbeat();
        break;

      case 'cmd.create_task': {
        const payload = Buffer.alloc(2);
        payload[0] = cmd.task_type;
        payload[1] = cmd.param;
        this.serial.write(encodeCommand(CmdId.CreateTask, payload, this.txSeq++, this.psk));
        break;
      }

      case 'cmd.delete_task': {
        const payload = Buffer.alloc(1);
        payload[0] = cmd.slot_index;
        this.serial.write(encodeCommand(CmdId.DeleteTask, payload, this.txSeq++, this.psk));
        break;
      }

      case 'cmd.vision_frame':
        void this.handleVisionFrame(cmd.jpeg_b64);
        break;
    }
  }

  private async handleVisionFrame(jpegB64: string): Promise<void> {
    const now = Date.now();
    const minIntervalMs = Math.floor(1000 / Math.max(1, Config.visionMaxHz));
    if (now - this.lastVisionTxMs < minIntervalMs) {
      log('debug', 'vision frame throttled');
      return;
    }
    this.lastVisionTxMs = now;

    const started = Date.now();
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), VISION_TIMEOUT_MS);

    try {
      const response = await fetch(Config.inferenceUrl, {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ jpeg_b64: jpegB64 }),
        signal: controller.signal,
      });

      if (!response.ok) {
        throw new Error(`inference HTTP ${response.status}`);
      }

      const result = await response.json() as InferenceResponse;
      const classId = result.class_id === 1 ? 1 : 0;
      const confidence = clampPercent(Math.floor(result.confidence * 100));
      const payload = Buffer.from([classId, confidence]);

      this.serial.write(encodeCommand(CmdId.DetectionResult, payload, this.txSeq++, this.psk));
      this.ws.broadcast({
        type: 'evt.detection',
        class_id: classId,
        class_name: result.class_name || (classId === 1 ? 'person' : 'none'),
        confidence,
        latency_ms: Date.now() - started,
      });
    } catch (err) {
      const message = err instanceof Error ? err.message : 'inference failed';
      log('warn', 'vision inference failed', { message });
      this.ws.broadcast({ type: 'evt.error', message: `vision inference failed: ${message}` });
    } finally {
      clearTimeout(timeout);
    }
  }

  // ---- Periodic heartbeat ---------------------------------------------------

  private sendHeartbeat(): void {
    const payload = Buffer.alloc(4);
    payload.writeUInt32LE((Date.now() & 0xffffffff) >>> 0, 0);
    this.serial.write(encodeCommand(CmdId.Heartbeat, payload, this.txSeq++, this.psk));
  }
}

function hex(n: number): string { return `0x${n.toString(16).padStart(2, '0')}`; }

function clampPercent(value: number): number {
  if (!Number.isFinite(value)) return 0;
  if (value < 0) return 0;
  if (value > 100) return 100;
  return value;
}
