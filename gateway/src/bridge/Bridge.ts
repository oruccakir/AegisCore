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
const STATE_NUM: Record<string, number> = {
  idle: 0, search: 1, track: 2, fail_safe: 3,
};

const HB_INTERVAL_MS = 1_000;
const PSK_HEX = Config.pskHex;

export class Bridge {
  private serial:    SerialBridge;
  private ws:        WsServer;
  private psk:       Buffer;
  private txSeq      = 0;
  private hbTimer:   ReturnType<typeof setInterval> | null = null;

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
          type:                 'evt.telemetry',
          state:                frame.payload[0] ?? 0,
          cpu_load_x10:         frame.payload.readUInt16LE(1),
          free_stack_min_words: frame.payload.readUInt16LE(3),
          hb_miss_count:        frame.payload[5] ?? 0,
        });
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
      case 'cmd.set_state': {
        const payload = Buffer.alloc(1);
        payload[0] = STATE_NUM[cmd.targetState] ?? 0;
        this.serial.write(encodeCommand(CmdId.SetState, payload, this.txSeq++, this.psk));
        break;
      }
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
