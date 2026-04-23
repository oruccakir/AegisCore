/**
 * STM32 edge simulator over TCP (bridged to a pty by socat).
 *
 * Usage (three terminals):
 *
 *   T1 — socat bridge:
 *     socat -d pty,raw,echo=0,link=/tmp/gw_pty tcp-listen:5555,reuseaddr,fork
 *
 *   T2 — this script:
 *     npx tsx gateway/tools/sim.ts
 *
 *   T3 — gateway:
 *     cd gateway && SERIAL_PORT=/tmp/gw_pty npm run dev
 *
 *   T4 — wscat:
 *     wscat --connect ws://localhost:8443 --subprotocol "ac2.v2"
 */

import net           from 'node:net';
import { encodeTelemetry, CmdId } from '../src/serial/AC2Framer.js';
import { AC2Parser }              from '../src/serial/AC2Parser.js';
import { hmacVerify }             from '../src/serial/Hmac.js';

const SIM_HOST = process.env['SIM_HOST'] ?? '127.0.0.1';
const SIM_PORT = Number(process.env['SIM_PORT'] ?? '5555');
const PSK      = Buffer.from(
  process.env['AC2_PSK'] ?? 'DEADBEEFCAFEBABE0123456789ABCDEF',
  'hex',
);

const LABEL: Record<number, string> = {
  0: 'IDLE', 1: 'SEARCH', 2: 'TRACK', 3: 'FAIL_SAFE',
};

let state    = 0;
let uptimeMs = 0;
let txSeq    = 0;
let socket: net.Socket | null = null;

function send(buf: Buffer): void {
  if (socket && !socket.destroyed) socket.write(buf);
}

// ---- TX helpers -------------------------------------------------------------

function txHeartbeat(): void {
  const p = Buffer.alloc(4);
  p.writeUInt32LE(uptimeMs, 0);
  send(encodeTelemetry(CmdId.Heartbeat, p, txSeq++));
  console.log(`[sim] → HEARTBEAT   uptime=${uptimeMs} ms`);
}

function txTelemetry(): void {
  const p = Buffer.alloc(6);
  p[0] = state;
  p.writeUInt16LE(55, 1);
  p.writeUInt16LE(300, 3);
  p[5] = 0;
  send(encodeTelemetry(CmdId.TelemetryTick, p, txSeq++));
  console.log(`[sim] → TELEMETRY   state=${LABEL[state] ?? '?'}`);
}

function txReportState(prev: number): void {
  const p = Buffer.alloc(6);
  p[0] = state; p[1] = prev;
  p.writeUInt32LE(uptimeMs, 2);
  send(encodeTelemetry(CmdId.ReportState, p, txSeq++));
  console.log(`[sim] → REPORT_STATE  ${LABEL[prev] ?? '?'} → ${LABEL[state] ?? '?'}`);
}

function txAck(echoedSeq: number): void {
  const p = Buffer.alloc(4);
  p.writeUInt32LE(echoedSeq, 0);
  send(encodeTelemetry(CmdId.Ack, p, txSeq++));
  console.log(`[sim] → ACK   echoed_seq=${echoedSeq}`);
}

function txVersionReport(): void {
  const p = Buffer.alloc(15);
  p[0] = 1; p[1] = 0; p[2] = 0;
  Buffer.from('SIMULA01', 'ascii').copy(p, 3);
  p.writeUInt32LE(1_745_409_985, 11);
  send(encodeTelemetry(CmdId.VersionReport, p, txSeq++));
  console.log('[sim] → VERSION_REPORT');
}

// ---- RX handler -------------------------------------------------------------

const parser = new AC2Parser();
parser.onFrame((frame) => {
  if (!frame.crcOk) { console.warn('[sim] ← bad CRC — dropped'); return; }
  if (!hmacVerify(PSK, frame.cmd, frame.payload, frame.hmac)) {
    console.warn('[sim] ← HMAC fail — dropped'); return;
  }

  console.log(`[sim] ← cmd=0x${frame.cmd.toString(16).padStart(2, '0')}  seq=${frame.seq}`);

  switch (frame.cmd) {
    case CmdId.Heartbeat:
      console.log('[sim] ← heartbeat from gateway');
      break;
    case CmdId.SetState: {
      const t = frame.payload[0] ?? 0;
      if (t > 3) break;
      const prev = state; state = t;
      txAck(frame.seq); txReportState(prev);
      break;
    }
    case CmdId.ManualLock:
      txAck(frame.seq);
      break;
    case CmdId.GetVersion:
      txAck(frame.seq); txVersionReport();
      break;
    default:
      console.warn(`[sim] ← unknown cmd 0x${frame.cmd.toString(16)}`);
  }
});

// ---- TCP connection ----------------------------------------------------------

function connect(): void {
  const sock = net.createConnection(SIM_PORT, SIM_HOST, () => {
    console.log(`[sim] connected to socat ${SIM_HOST}:${SIM_PORT}`);
    socket = sock;
    startTick();
  });

  sock.on('data', (chunk: Buffer) => {
    console.log(`[sim] ← raw ${chunk.length} bytes`);
    parser.feedBuffer(chunk);
  });

  sock.on('error', (err) => console.error('[sim] tcp error:', err.message));

  sock.on('close', () => {
    console.warn('[sim] connection closed — retrying in 2s');
    socket = null;
    setTimeout(connect, 2000);
  });
}

let tickStarted = false;
function startTick(): void {
  if (tickStarted) return;
  tickStarted = true;
  setInterval(() => {
    uptimeMs += 1000;
    txHeartbeat();
    txTelemetry();
  }, 1000);
}

connect();
console.log('[sim] running — Ctrl+C to stop');
