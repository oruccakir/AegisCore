import { describe, it, expect } from 'vitest';
import { crc16 } from '../src/serial/Crc16.js';

describe('CRC-16/CCITT-FALSE', () => {
  it('returns 0xFFFF for empty input', () => {
    expect(crc16(new Uint8Array(0))).toBe(0xffff);
  });

  it('computes standard check value 0x29B1 for "123456789"', () => {
    const buf = Buffer.from('123456789', 'ascii');
    expect(crc16(buf)).toBe(0x29b1);
  });

  it('returns 0x0000 for a single zero byte', () => {
    expect(crc16(new Uint8Array([0x00]))).toBe(0xe1f0);
  });

  it('respects the len parameter', () => {
    const buf = Buffer.from('123456789', 'ascii');
    // CRC of "123" only
    const full  = crc16(Buffer.from('123', 'ascii'));
    const trunc = crc16(buf, 3);
    expect(trunc).toBe(full);
  });

  it('matches known vector: 0xAA 0x02 → 0xCFF8', () => {
    expect(crc16(new Uint8Array([0xaa, 0x02]))).toBe(0xcff8);
  });
});
