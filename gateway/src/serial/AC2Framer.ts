// AC2 frame format (IRS §3.1):
// SYNC(0xAA) | VER(0x02) | SEQ[4 LE] | LEN[1] | CMD[1] | PAYLOAD[0-48] | HMAC[8] | CRC[2 LE]
// Total overhead = 18 bytes; max frame = 66 bytes.

import { crc16 }  from './Crc16.js';
import { hmac8 }  from './Hmac.js';

export const SYNC    = 0xaa;
export const VERSION = 0x02;
export const MAX_PAYLOAD = 48;
export const OVERHEAD    = 18; // 1+1+4+1+1+8+2
export const MAX_FRAME   = OVERHEAD + MAX_PAYLOAD; // 66

export const CmdId = {
  GetVersion:    0x01,
  VersionReport: 0x02,
  SetState:      0x10,
  ManualLock:    0x11,
  ResetAck:      0x12,
  ReportState:   0x20,
  TelemetryTick: 0x21,
  FaultReport:   0x30,
  AuditEvent:    0x31,
  Ack:           0x80,
  Nack:          0x81,
  Heartbeat:     0x99,
} as const;
export type CmdId = typeof CmdId[keyof typeof CmdId];

export const ErrCode = {
  InvalidCmd:        0x01,
  InvalidPayload:    0x02,
  InvalidTransition: 0x03,
  AuthFail:          0x04,
  Replay:            0x05,
  RateLimited:       0x06,
  FailSafeLock:      0x07,
  Busy:              0x08,
} as const;

/** Build the fixed 8-byte header into dst[offset..]. Returns bytes written (always 8). */
function writeHeader(
  dst: Buffer,
  offset: number,
  seq: number,
  payloadLen: number,
  cmd: number,
): number {
  dst[offset]     = SYNC;
  dst[offset + 1] = VERSION;
  dst.writeUInt32LE(seq, offset + 2);
  dst[offset + 6] = payloadLen;
  dst[offset + 7] = cmd;
  return 8;
}

/**
 * Encode a Host→Edge command frame with a real HMAC.
 * Returns a new Buffer containing the complete frame.
 */
export function encodeCommand(
  cmd: number,
  payload: Uint8Array,
  seq: number,
  psk: Buffer,
): Buffer {
  if (payload.length > MAX_PAYLOAD) throw new RangeError('payload exceeds 48 bytes');
  const frameLen = OVERHEAD + payload.length;
  const buf = Buffer.allocUnsafe(frameLen);
  let pos = 0;
  pos += writeHeader(buf, pos, seq, payload.length, cmd);
  buf.set(payload, pos);
  pos += payload.length;
  const mac = hmac8(psk, cmd, payload);
  mac.copy(buf, pos);
  pos += 8;
  // CRC covers everything before the CRC field.
  const crc = crc16(buf, pos);
  buf.writeUInt16LE(crc, pos);
  return buf;
}

/**
 * Encode an Edge→Host telemetry frame (HMAC bytes = 0x00).
 * Used by tests and by the gateway when constructing outgoing telemetry-style frames.
 */
export function encodeTelemetry(
  cmd: number,
  payload: Uint8Array,
  seq: number,
): Buffer {
  if (payload.length > MAX_PAYLOAD) throw new RangeError('payload exceeds 48 bytes');
  const frameLen = OVERHEAD + payload.length;
  const buf = Buffer.allocUnsafe(frameLen);
  let pos = 0;
  pos += writeHeader(buf, pos, seq, payload.length, cmd);
  buf.set(payload, pos);
  pos += payload.length;
  buf.fill(0, pos, pos + 8); // zero HMAC
  pos += 8;
  const crc = crc16(buf, pos);
  buf.writeUInt16LE(crc, pos);
  return buf;
}
