# Gateway — AC2Framer.ts (Frame Encoder)

**File:** `gateway/src/serial/AC2Framer.ts`

---

## What this file is

`AC2Framer.ts` contains the frame encoder — it takes a command ID, payload, sequence number, and PSK, and builds a complete binary AC2 frame ready to send over UART.

It also defines the command and error code constants (`CmdId`, `ErrCode`) that are shared across the gateway.

---

## Frame layout (IRS §3.1)

```
Byte offset:  0      1      2..5      6       7       8..N     N+1..N+8    N+9..N+10
              SYNC   VER    SEQ[4]    LEN     CMD     PAYLOAD  HMAC[8]     CRC[2]
              0xAA   0x02   uint32LE  uint8   uint8   0-48B    8B          uint16LE
```

Total frame size = 18 (overhead) + payload length.
Maximum frame size = 18 + 48 = 66 bytes.

The CRC covers all bytes from SYNC through the last HMAC byte (everything except the CRC itself).

---

## Constants

```typescript
export const SYNC    = 0xaa;
export const VERSION = 0x02;
export const MAX_PAYLOAD = 48;
export const OVERHEAD    = 18; // 1+1+4+1+1+8+2
export const MAX_FRAME   = OVERHEAD + MAX_PAYLOAD; // 66
```

`export const` — exports the constant so other modules can import it. `const` in JavaScript/TypeScript means the binding cannot be reassigned (`SYNC = 0xff` would be a compile error). The value itself is a primitive, so it is inherently immutable.

`OVERHEAD = 18` breakdown: 1 (SYNC) + 1 (VER) + 4 (SEQ) + 1 (LEN) + 1 (CMD) + 8 (HMAC) + 2 (CRC) = 18.

---

## CmdId

```typescript
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
```

`as const` — makes all values read-only literal types. Without it, TypeScript infers `GetVersion: number`. With it, `CmdId.GetVersion` has type `0x01` (the literal number 1).

`typeof CmdId[keyof typeof CmdId]` — this is a TypeScript type trick to extract a union of all value types. `keyof typeof CmdId` is `'GetVersion' | 'VersionReport' | ...`. `typeof CmdId[...]` gives the type of each value. Result: `0x01 | 0x02 | 0x10 | ...`. So the `CmdId` type is "any valid command ID number."

These values must match the `CmdId` enum in `edge/app/telemetry.hpp`. If they diverge, commands get misrouted on the edge.

---

## writeHeader()

```typescript
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
```

`dst[offset] = SYNC` — writes byte value 0xAA at the specified offset in the Buffer.

`dst.writeUInt32LE(seq, offset + 2)` — writes a 32-bit unsigned integer in little-endian byte order. On x86 and ARM (both little-endian), the in-memory representation is the same as the wire format. The `seq` value 0x00000001 becomes bytes `01 00 00 00` in the buffer.

Returns `8` — the number of bytes written. The caller uses this as `pos += writeHeader(...)` to advance the write position.

---

## encodeCommand()

```typescript
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
  const crc = crc16(buf, pos);
  buf.writeUInt16LE(crc, pos);
  return buf;
}
```

This is the command encoder — used for gateway → edge frames. It computes a real HMAC.

`Buffer.allocUnsafe(frameLen)` — allocates exactly `frameLen` bytes. Unsafe (unzeroed) is fine because we fill every byte explicitly below.

`buf.set(payload, pos)` — copies the payload bytes into the buffer starting at `pos`. `Buffer.set` is the Node.js way of doing `memcpy`.

`hmac8(psk, cmd, payload)` — computes the 8-byte truncated HMAC. See `gw_09_hmac.md`.

`mac.copy(buf, pos)` — copies the 8-byte MAC into the frame buffer at the HMAC field position.

`crc16(buf, pos)` — computes CRC-16/CCITT-FALSE over the first `pos` bytes of `buf` (everything before the CRC field). `pos` at this point points to where the CRC bytes will go, so it correctly covers all previous bytes.

`buf.writeUInt16LE(crc, pos)` — writes the 16-bit CRC in little-endian order. The STM32 reads it with `readUInt16LE` equivalent (`*(uint16_t*)ptr` on little-endian ARM).

---

## encodeTelemetry()

```typescript
export function encodeTelemetry(
  cmd: number,
  payload: Uint8Array,
  seq: number,
): Buffer {
  ...
  buf.fill(0, pos, pos + 8); // zero HMAC
  ...
}
```

Like `encodeCommand` but with zeroed HMAC. Used for edge → host telemetry frames (and in tests). The edge sends these with no HMAC; the gateway detects `hmac.some(b => b !== 0) === false` and skips verification.

`buf.fill(0, pos, pos + 8)` — fills bytes from `pos` to `pos + 8` (exclusive) with 0x00. The three-argument form of `fill` specifies start and end offsets.
