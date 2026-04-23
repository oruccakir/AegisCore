import { createHmac, timingSafeEqual } from 'node:crypto';

const TRUNC = 8; // AC2 uses first 8 bytes of HMAC-SHA-256

/** Compute 8-byte truncated HMAC-SHA-256 over (cmdId | payload). */
export function hmac8(psk: Buffer, cmdId: number, payload: Uint8Array): Buffer {
  const body = Buffer.allocUnsafe(1 + payload.length);
  body[0] = cmdId;
  body.set(payload, 1);
  const digest = createHmac('sha256', psk).update(body).digest();
  return digest.subarray(0, TRUNC) as Buffer;
}

/** Constant-time comparison of two HMAC truncations. */
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
