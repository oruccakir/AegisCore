# Gateway — Crc16.ts

**File:** `gateway/src/serial/Crc16.ts`

---

## What this file is

The CRC-16/CCITT-FALSE algorithm, implemented as a lookup table for speed. This is the TypeScript equivalent of `edge/app/crc16.cpp`. Both implementations produce identical results for the same input — which is required because the edge computes the CRC and the gateway verifies it.

---

## Algorithm parameters

CRC-16/CCITT-FALSE:

- Polynomial: 0x1021
- Initial value: 0xFFFF
- Input reflection: no (process bits MSB first)
- Output reflection: no
- XOR output: 0x0000

The "FALSE" in the name refers to the fact that unlike some other CRC-16 variants, this one does NOT reflect input bytes. It processes the most-significant bit first.

For a deeper explanation of these terms, see `code_doc/12_crc16.md` which covers the same algorithm in the C++ context.

---

## Lookup table

```typescript
const TABLE = (() => {
  const t = new Uint16Array(256);
  for (let i = 0; i < 256; i++) {
    let crc = i << 8;
    for (let j = 0; j < 8; j++) {
      crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
    }
    t[i] = crc & 0xffff;
  }
  return t;
})();
```

`(() => { ... })()` — an Immediately Invoked Function Expression (IIFE). The function is defined and called in the same expression. This is used to compute `TABLE` at module load time, once. The result (a filled `Uint16Array`) is stored in the constant.

`new Uint16Array(256)` — a typed array of 256 16-bit unsigned integers, initialized to zero. Typed arrays are faster than regular JavaScript arrays for numeric work because the values are stored as actual 16-bit numbers in contiguous memory, not as boxed JavaScript objects.

`let crc = i << 8` — for each possible byte value `i` (0–255), computes what the CRC contribution of that byte would be. The byte is placed in the high byte of a 16-bit value (shifted left by 8).

`(crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1)` — the core CRC bit operation. If the most significant bit is set, XOR with the polynomial after shifting left. Otherwise, just shift left. Runs 8 times (once per bit).

`t[i] = crc & 0xffff` — store the 16-bit result. `& 0xffff` masks to 16 bits because JavaScript bitwise operations can produce values outside the 16-bit range (JavaScript numbers are 64-bit floats, and shifts can produce values with bit 31 set that look negative if not masked).

---

## crc16() function

```typescript
export function crc16(buf: Uint8Array, len = buf.length): number {
  let crc = 0xffff;
  for (let i = 0; i < len; i++) {
    const b = buf[i];
    if (b === undefined) break;
    crc = ((crc << 8) ^ (TABLE[((crc >> 8) ^ b) & 0xff]!)) & 0xffff;
  }
  return crc;
}
```

`len = buf.length` — default parameter. If you call `crc16(buf)` without a second argument, `len` defaults to `buf.length`. If you call `crc16(buf, 10)`, only the first 10 bytes are processed. `AC2Parser` uses this to compute CRC over only the valid bytes in its pre-allocated buffer.

`let crc = 0xffff` — initialize the accumulator to 0xFFFF (the init value of this CRC variant).

`((crc >> 8) ^ b) & 0xff` — compute the lookup table index. Take the high byte of the current CRC (`crc >> 8`), XOR it with the incoming byte `b`, and mask to 8 bits (`& 0xff`) to get an index 0–255.

`TABLE[...] !` — the `!` is a TypeScript non-null assertion. TypeScript cannot prove the array access will succeed. `!` tells it to trust us that the index is valid (0–255, which it always is after `& 0xff`).

`(crc << 8) ^ table_value` — shift the current CRC left by 8 bits (dropping the old high byte) and XOR in the table-looked-up value.

`& 0xffff` — mask to 16 bits. JavaScript arithmetic operates on 32-bit integers internally for bitwise ops; the mask keeps the result in 16-bit range.

Final `return crc` — the 16-bit CRC value as a JavaScript number (0–65535).

---

## Why a lookup table instead of bit-by-bit?

The bit-by-bit algorithm processes 8 operations per byte. The table approach precomputes all 256 possible byte contributions and performs only 3 operations per byte (shift, XOR, array lookup). For the AC2 frames we send (up to 64 bytes), the difference is small, but the table approach is the standard in protocol implementations.
