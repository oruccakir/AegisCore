# Gateway — Hmac.ts

**File:** `gateway/src/serial/Hmac.ts`

---

## What this file is

Two functions: one to compute an 8-byte HMAC-SHA-256, one to verify it. The gateway uses Node.js's built-in `crypto` module rather than implementing SHA-256 from scratch (unlike the edge firmware which has a software implementation in `hmac_sha256.cpp`).

---

## Why Node.js crypto vs software SHA-256

The edge firmware implements SHA-256 in pure C++ because it has no OS and cannot call library functions at runtime. The gateway runs in Node.js, which has `node:crypto` — a binding to OpenSSL. Using it is:

- **Faster** — OpenSSL uses SIMD instructions and is heavily optimized.
- **Safer** — A well-audited implementation with no side-channel vulnerabilities.
- **Simpler** — 3 lines of code instead of 200.

The two implementations must produce identical output (they do — both implement the HMAC-SHA-256 standard).

---

## hmac8()

```typescript
const TRUNC = 8; // AC2 uses first 8 bytes of HMAC-SHA-256

export function hmac8(psk: Buffer, cmdId: number, payload: Uint8Array): Buffer {
  const body = Buffer.allocUnsafe(1 + payload.length);
  body[0] = cmdId;
  body.set(payload, 1);
  const digest = createHmac('sha256', psk).update(body).digest();
  return digest.subarray(0, TRUNC) as Buffer;
}
```

**What gets hashed?** The HMAC message body is: `[cmdId byte] + [payload bytes]`. Not the full frame — just CMD and PAYLOAD. The SEQ, LEN, SYNC, VER fields are not included. This matches the edge firmware's `HMAC_SHA256_Compute(cmd, payload, hmac_out)`.

`createHmac('sha256', psk)` — creates an HMAC-SHA-256 context initialized with the PSK as the key. `'sha256'` is the hash algorithm name.

`.update(body)` — feeds the message body into the HMAC computation. You can call `update()` multiple times to feed data in chunks, but here we do it in one call.

`.digest()` — finalizes the computation and returns a 32-byte Buffer (256 bits = the full SHA-256 output).

`digest.subarray(0, TRUNC)` — takes only the first 8 bytes of the 32-byte HMAC. This is the truncation to 64 bits that AC2 uses.

`as Buffer` — TypeScript type assertion. `subarray` returns a `Buffer` but TypeScript types it as `Uint8Array` in some versions. The assertion tells the compiler it is a Buffer.

---

## hmacVerify()

```typescript
export function hmacVerify(
  psk: Buffer,
  cmdId: number,
  payload: Uint8Array,
  expected: Uint8Array,
): boolean {
  const computed = hmac8(psk, cmdId, payload);
  if (expected.length < TRUNC) return false;
  return timingSafeEqual(computed, expected.subarray(0, TRUNC));
}
```

`timingSafeEqual(a, b)` — compares two buffers in constant time. This is a security requirement.

**Why constant time?** A naive comparison `computed[0] === expected[0] && computed[1] === expected[1]...` returns `false` as soon as the first mismatch is found. An attacker can measure how long the comparison took: if it returns quickly, the first byte was wrong; if it takes longer, more bytes matched. By timing thousands of attempts, the attacker can recover the correct HMAC byte by byte. `timingSafeEqual` always compares all bytes regardless of where the first mismatch is, so no timing information leaks.

`if (expected.length < TRUNC) return false` — safety check. If the expected HMAC is shorter than 8 bytes, reject immediately. This avoids `subarray(0, 8)` on a shorter buffer.

`expected.subarray(0, TRUNC)` — takes the first 8 bytes of the expected value. The frame may pass the full 8-byte HMAC field; `subarray` creates a view without copying.

---

## Comparison with edge firmware hmac_sha256.cpp

The edge firmware's `HMAC_SHA256_Compute()` builds the HMAC manually:

1. XOR the key with the ipad (0x36 repeated)
2. Hash: `SHA256(ipad_key || message)`
3. XOR the key with the opad (0x5C repeated)
4. Hash: `SHA256(opad_key || inner_hash)`
5. Truncate to 8 bytes

Node.js's `createHmac('sha256', key).update(body).digest()` does the same thing internally. The results are identical for the same key and message.
