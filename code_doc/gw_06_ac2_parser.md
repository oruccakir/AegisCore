# Gateway — AC2Parser.ts

**File:** `gateway/src/serial/AC2Parser.ts`

---

## What this file is

`AC2Parser` is a byte-by-byte state machine. You feed it one byte at a time. When it has accumulated all bytes of a complete, valid AC2 frame, it calls the registered callback with a parsed `AC2Frame` object.

It is the mirror of `edge/app/ac2_framer.cpp`'s `AC2Parser::Feed()`. Both implement the exact same state machine logic — one in TypeScript, one in C++. This guarantees they parse the same byte stream identically.

---

## AC2Frame interface

```typescript
export interface AC2Frame {
  seq:        number;
  cmd:        number;
  payload:    Buffer;
  hmac:       Buffer;
  crcOk:      boolean;
}
```

This is the parsed output. `interface` in TypeScript defines the shape of an object — like a C struct without the memory layout concerns. All fields are present; `crcOk` is false if the CRC bytes did not match the computed checksum.

---

## States

```typescript
const enum St {
  WaitSync, WaitVersion,
  WaitSeq0, WaitSeq1, WaitSeq2, WaitSeq3,
  WaitLength, WaitCmd,
  WaitPayload, WaitHmac, WaitCrc0, WaitCrc1,
}
```

`const enum` — TypeScript optimizes this away at compile time. Instead of generating a JavaScript object, it replaces every `St.WaitSync` with the literal number `0`, `St.WaitVersion` with `1`, etc. Zero runtime overhead — the compiled JS just has `0`, `1`, `2`... in the switch statement.

12 states for parsing a frame:
1. `WaitSync` — looking for 0xAA start byte
2. `WaitVersion` — expect 0x02 version byte
3. `WaitSeq0..3` — accumulate 4 bytes of the sequence number (little-endian uint32)
4. `WaitLength` — payload length byte (rejected if > 48)
5. `WaitCmd` — command ID byte
6. `WaitPayload` — collect `payloadLen` bytes
7. `WaitHmac` — collect 8 HMAC bytes
8. `WaitCrc0` — first CRC byte (low)
9. `WaitCrc1` — second CRC byte (high); verify and dispatch

---

## Pre-allocated buffers

```typescript
private readonly raw     = Buffer.allocUnsafe(MAX_FRAME);
private readonly payload = Buffer.allocUnsafe(MAX_PAYLOAD);
private readonly hmac    = Buffer.allocUnsafe(HMAC_LEN);
```

`readonly` — these Buffer objects are assigned once and never reassigned. The bytes inside them change as data arrives, but the Buffer references themselves are constant.

`Buffer.allocUnsafe(n)` — allocates n bytes without zeroing them. Faster than `Buffer.alloc(n)` which zeros the memory. Safe here because we track how many bytes are valid with `rawLen`, `payloadIdx`, `hmacIdx`.

`MAX_FRAME = 66` — maximum bytes in one AC2 frame (18 overhead + 48 max payload).

These buffers are reused for every frame — no heap allocation per frame. This matches the embedded philosophy: allocate once, reuse forever.

---

## feed(byte) — the state machine

```typescript
feed(byte: number): void {
  switch (this.state) {
    case St.WaitSync:
      if (byte === SYNC) { this.rawLen = 0; this.push(byte); this.state = St.WaitVersion; }
      break;
    ...
  }
}
```

**WaitSync:** Discards all bytes until 0xAA is seen. Resets `rawLen = 0` to start accumulating the raw frame bytes fresh. `this.push(byte)` appends the byte to `raw[]`.

**WaitVersion:** If the next byte is 0x02, proceed. Anything else → `reset()` (go back to WaitSync). This prevents accidentally syncing on a 0xAA byte inside a payload.

**Sequence number (4 states):**

```typescript
case St.WaitSeq0:
  this.seq = byte; this.push(byte); this.state = St.WaitSeq1;
  break;
case St.WaitSeq1:
  this.seq |= byte << 8; this.push(byte); this.state = St.WaitSeq2;
  break;
case St.WaitSeq2:
  this.seq |= byte << 16; this.push(byte); this.state = St.WaitSeq3;
  break;
case St.WaitSeq3:
  this.seq = ((this.seq | (byte << 24)) >>> 0); this.push(byte); this.state = St.WaitLength;
  break;
```

Little-endian reconstruction: the first byte goes into bits 0-7, second into bits 8-15, etc.

`byte << 24` — JavaScript shifts are 32-bit signed. If bit 31 is set, the result is negative. `>>> 0` converts to unsigned 32-bit integer. Without this, sequences above 2^31 would appear as negative numbers in JavaScript.

**Length check:**

```typescript
case St.WaitLength:
  if (byte > MAX_PAYLOAD) { this.reset(); break; } // SR-08
  this.payloadLen = byte; this.payloadIdx = 0;
  this.state = St.WaitCmd;
  break;
```

`if (byte > MAX_PAYLOAD)` — SR-08 (security requirement): reject frames claiming a payload larger than 48 bytes. Without this, a malformed frame could cause out-of-bounds writes.

**WaitCmd:** After reading the CMD byte, decides the next state based on whether there is payload to collect:

```typescript
this.state = this.payloadLen > 0 ? St.WaitPayload : St.WaitHmac;
```

If `payloadLen` is 0 (e.g. a GetVersion command with no payload), skip straight to collecting HMAC bytes.

**CRC verification (WaitCrc1):**

```typescript
case St.WaitCrc1: {
  const rxCrc   = this.rxCrcLo | (byte << 8);
  const calcCrc = crc16(this.raw, this.rawLen);
  const ok      = rxCrc === calcCrc;
  if (!ok) this.crcErrors++;
  this.frameCount++;
  this.dispatch(ok);
  this.reset();
  break;
}
```

`this.rxCrcLo | (byte << 8)` — reassembles the 16-bit CRC from two bytes. `rxCrcLo` was stored in `WaitCrc0`; `byte` is the high byte. Note the CRC bytes are not accumulated into `raw[]` — `this.push()` is not called for CRC bytes. This is correct: the CRC is computed over all bytes *before* the CRC field.

`crc16(this.raw, this.rawLen)` — computes CRC over `rawLen` bytes. Passes `rawLen` explicitly because the buffer may have capacity for 66 bytes but only `rawLen` bytes are valid.

---

## dispatch()

```typescript
private dispatch(crcOk: boolean): void {
  if (!this.cb) return;
  const frame: AC2Frame = {
    seq:     this.seq,
    cmd:     this.cmd,
    payload: Buffer.from(this.payload.subarray(0, this.payloadLen)),
    hmac:    Buffer.from(this.hmac.subarray(0, HMAC_LEN)),
    crcOk,
  };
  this.cb(frame);
}
```

`Buffer.from(this.payload.subarray(0, this.payloadLen))` — creates a *copy* of the payload bytes. `subarray` creates a view into the same memory (no copy). `Buffer.from()` then makes an independent copy. This is important: the pre-allocated `payload` buffer will be overwritten by the next frame. The callback receives a fresh buffer that it can safely hold on to after `feed()` returns.

The same copy is made for `hmac`. The `seq`, `cmd`, and `crcOk` fields are primitive values, so they are naturally copied.

---

## feedBuffer()

```typescript
feedBuffer(buf: Buffer | Uint8Array): void {
  for (let i = 0; i < buf.length; i++) this.feed(buf[i]!);
}
```

Convenience wrapper — loops over a chunk of bytes calling `feed()` for each one. Used by `SerialBridge` when the `'data'` event arrives with multiple bytes at once.
