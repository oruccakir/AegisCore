import { describe, it, expect } from 'vitest';
import { AC2Parser, AC2Frame }  from '../src/serial/AC2Parser.js';
import { encodeCommand, encodeTelemetry, CmdId } from '../src/serial/AC2Framer.js';

const PSK  = Buffer.from('DEADBEEFCAFEBABE0123456789ABCDEF', 'hex');
const ZERO_PSK = Buffer.alloc(16, 0);

function feedFrame(parser: AC2Parser, buf: Buffer): AC2Frame[] {
  const frames: AC2Frame[] = [];
  parser.onFrame((f) => frames.push(f));
  parser.feedBuffer(buf);
  return frames;
}

describe('AC2Parser', () => {
  it('decodes a telemetry frame (zero HMAC) correctly', () => {
    const payload = Buffer.from([0x01, 0x00, 0x00, 0x64, 0x00, 0x00]);
    const frame   = encodeTelemetry(CmdId.TelemetryTick, payload, 42);
    const parser  = new AC2Parser();
    const frames  = feedFrame(parser, frame);
    expect(frames).toHaveLength(1);
    expect(frames[0]!.cmd).toBe(CmdId.TelemetryTick);
    expect(frames[0]!.seq).toBe(42);
    expect(frames[0]!.crcOk).toBe(true);
    expect(frames[0]!.payload).toEqual(payload);
  });

  it('decodes a command frame with real HMAC', () => {
    const payload = Buffer.from([0x01]); // lock = true
    const frame   = encodeCommand(CmdId.ManualLock, payload, 7, PSK);
    const parser  = new AC2Parser();
    const frames  = feedFrame(parser, frame);
    expect(frames[0]!.crcOk).toBe(true);
    expect(frames[0]!.cmd).toBe(CmdId.ManualLock);
    expect(frames[0]!.seq).toBe(7);
  });

  it('rejects a frame with a flipped CRC byte', () => {
    const frame = encodeTelemetry(CmdId.Heartbeat, Buffer.from([0x00, 0x00, 0x00, 0x00]), 1);
    frame[frame.length - 1] ^= 0xff; // corrupt CRC high byte
    const parser = new AC2Parser();
    const frames = feedFrame(parser, frame);
    expect(frames[0]!.crcOk).toBe(false);
    expect(parser.crcErrors).toBe(1);
  });

  it('rejects oversized payload (SR-08: LEN > 48 drops frame)', () => {
    // Manually craft a frame with LEN=49 (> MAX_PAYLOAD)
    const buf = Buffer.alloc(20);
    buf[0] = 0xaa; buf[1] = 0x02;   // SYNC, VER
    buf.writeUInt32LE(0, 2);         // SEQ
    buf[6] = 49;                      // LEN > 48 → should drop
    buf[7] = CmdId.TelemetryTick;
    const parser = new AC2Parser();
    const frames = feedFrame(parser, buf);
    expect(frames).toHaveLength(0);
  });

  it('handles back-to-back frames in a single buffer', () => {
    const f1 = encodeTelemetry(CmdId.Heartbeat, Buffer.from([0, 0, 0, 0]), 1);
    const f2 = encodeTelemetry(CmdId.Heartbeat, Buffer.from([0, 0, 0, 0]), 2);
    const combined = Buffer.concat([f1, f2]);
    const parser   = new AC2Parser();
    const frames   = feedFrame(parser, combined);
    expect(frames).toHaveLength(2);
    expect(frames[0]!.seq).toBe(1);
    expect(frames[1]!.seq).toBe(2);
  });

  it('resynchronises after garbage bytes', () => {
    const garbage = Buffer.from([0x00, 0xff, 0x12, 0x34]);
    const valid   = encodeTelemetry(CmdId.Heartbeat, Buffer.from([0, 0, 0, 0]), 5);
    const combined = Buffer.concat([garbage, valid]);
    const parser   = new AC2Parser();
    const frames   = feedFrame(parser, combined);
    expect(frames[0]!.crcOk).toBe(true);
    expect(frames[0]!.seq).toBe(5);
  });
});

describe('AC2Framer', () => {
  it('encodeCommand produces a frame with correct length', () => {
    const payload = Buffer.from([0x01]);
    const frame   = encodeCommand(CmdId.ManualLock, payload, 0, PSK);
    expect(frame.length).toBe(18 + 1); // OVERHEAD + 1
  });

  it('encodes a 2-byte detection result command', () => {
    const payload = Buffer.from([0x01, 0x87]); // person, 87%
    const frame   = encodeCommand(CmdId.DetectionResult, payload, 9, PSK);
    const parser  = new AC2Parser();
    const frames  = feedFrame(parser, frame);
    expect(frames[0]!.cmd).toBe(CmdId.DetectionResult);
    expect(frames[0]!.payload).toEqual(payload);
  });

  it('encodeTelemetry produces a frame with all-zero HMAC', () => {
    const frame = encodeTelemetry(CmdId.Heartbeat, Buffer.from([0,0,0,0]), 0);
    // HMAC starts at byte 8 + 4 (payload) = 12; length 8
    const hmacSlice = frame.subarray(12, 20);
    expect(hmacSlice.every((b) => b === 0)).toBe(true);
  });

  it('throws RangeError for payload > 128 bytes', () => {
    expect(() => encodeCommand(CmdId.ManualLock, Buffer.alloc(129), 0, PSK))
      .toThrow(RangeError);
  });
});
