# HMAC-SHA-256 — Message Authentication

**Files:** `edge/app/hmac_sha256.hpp`, `edge/app/hmac_sha256.cpp`

---

## What this module is

**HMAC-SHA-256** is a way to verify that a message came from someone who knows a secret key, and that the message was not tampered with in transit.

AegisCore uses it to authenticate commands from the gateway: the gateway computes an HMAC over `CMD_ID + PAYLOAD` using a shared secret (PSK), and appends the first 8 bytes to the frame. The STM32 recomputes the HMAC and rejects any frame where they don't match.

This module is a **pure-software implementation** — no HAL crypto peripheral. This makes it portable and testable on any host machine.

---

## SHA-256 primer

SHA-256 is a one-way hash function:
- Input: any length message
- Output: always exactly 32 bytes (the "digest")
- Same input → always same output
- Tiny input change → completely different output
- Cannot reverse-engineer the input from the output

SHA-256 processes input in 64-byte blocks. If the input is not a multiple of 64 bytes, it is padded.

---

## HMAC construction

HMAC turns a plain hash into a message authentication code:

```
inner = SHA-256( (key XOR 0x36...36) || message )
HMAC  = SHA-256( (key XOR 0x5C...5C) || inner )
```

The two 64-byte pads (`ipad = 0x36` repeated, `opad = 0x5C` repeated) ensure that a hash of `key + message` cannot be extended by an attacker without knowing the key. AC2 uses only the first 8 bytes of the 32-byte HMAC output.

---

## Internal SHA-256 state

### `Sha256Ctx`

```cpp
struct Sha256Ctx
{
    std::uint32_t state[8];   // 8 running hash words (256 bits total)
    std::uint32_t lo;         // total bit count, low 32 bits
    std::uint32_t hi;         // total bit count, high 32 bits
    std::uint8_t  buf[64];    // accumulation buffer (one 512-bit block)
    std::uint32_t buf_len;    // bytes in buffer
};
```

SHA-256 maintains 8 × 32-bit state words (256 bits). After each 64-byte block, these 8 words are updated. The final digest is these 8 words concatenated.

### Round constants `kK[64]`

64 constants derived from the cube roots of the first 64 prime numbers. Used in the mixing function. These are fixed, defined by the SHA-256 standard.

### Initial state `kInitState[8]`

The 8 starting values of the SHA-256 state, derived from the square roots of the first 8 primes. Also fixed by the standard.

---

## Bitwise helper functions

These implement the standard SHA-256 mixing operations:

```cpp
Rotr(x, n)    // rotate right: (x >> n) | (x << (32-n))
Ch(x, y, z)   // "choose": (x & y) ^ (~x & z)
Maj(x, y, z)  // "majority": (x & y) ^ (x & z) ^ (y & z)
Sigma0(x)     // Rotr(x,2) ^ Rotr(x,13) ^ Rotr(x,22)
Sigma1(x)     // Rotr(x,6) ^ Rotr(x,11) ^ Rotr(x,25)
sigma0(x)     // Rotr(x,7) ^ Rotr(x,18) ^ (x>>3)
sigma1(x)     // Rotr(x,17) ^ Rotr(x,19) ^ (x>>10)
```

`Sigma0/1` (capital) are used in the main round function. `sigma0/1` (lowercase) are used in the message schedule expansion.

---

## `Sha256Transform(Sha256Ctx& ctx, const std::uint8_t* block)`

Processes one 64-byte block and updates `ctx.state`.

```cpp
// 1. Build message schedule W[0..63]
for i in 0..15:
    w[i] = 4 bytes from block, big-endian

for i in 16..63:
    w[i] = sigma1(w[i-2]) + w[i-7] + sigma0(w[i-15]) + w[i-16]
```

The first 16 words come directly from the block. The remaining 48 are derived from earlier words using the `sigma` functions — this "expands" the 512-bit block into 2048 bits of scheduled message.

```cpp
// 2. Initialise working variables from current state
a = state[0], b = state[1], ..., h = state[7]

// 3. 64 rounds of mixing
for i in 0..63:
    t1 = h + Sigma1(e) + Ch(e,f,g) + K[i] + w[i]
    t2 = Sigma0(a) + Maj(a,b,c)
    h=g, g=f, f=e, e=d+t1
    d=c, c=b, b=a, a=t1+t2

// 4. Add back to running state
state[0] += a, ..., state[7] += h
```

This is the core SHA-256 compression function. Each round depends on all previous rounds, creating an avalanche effect: a one-bit change in the input changes approximately half the output bits.

---

## `Sha256Init`, `Sha256Update`, `Sha256Final`

Standard streaming API:

- **`Sha256Init`** — set `state` to the initial constants, clear counters and buffer
- **`Sha256Update`** — feed more data. Handles data that spans block boundaries by buffering partial blocks
- **`Sha256Final`** — finalise the hash: append `0x80` padding, pad to 56 bytes, append 64-bit big-endian bit count, process last block, write 8 state words to output as 32 big-endian bytes

---

## `HMAC_SHA256(key, key_len, data, data_len, out[32])`

```cpp
void HMAC_SHA256(...) noexcept
{
    // Prepare padded key
    uint8_t k_ipad[64];  // key XOR 0x36...
    uint8_t k_opad[64];  // key XOR 0x5C...
    memset(k_ipad, 0x36, 64);
    memset(k_opad, 0x5C, 64);
    for (i = 0; i < key_len; ++i) {
        k_ipad[i] ^= key[i];  // XOR key bytes into the pad
        k_opad[i] ^= key[i];
    }

    // Inner hash: SHA-256(k_ipad || data)
    Sha256Init(ctx);
    Sha256Update(ctx, k_ipad, 64);
    Sha256Update(ctx, data, data_len);
    Sha256Final(ctx, inner);   // 32-byte inner digest

    // Outer hash: SHA-256(k_opad || inner)
    Sha256Init(ctx);
    Sha256Update(ctx, k_opad, 64);
    Sha256Update(ctx, inner, 32);
    Sha256Final(ctx, out);     // final 32-byte HMAC
}
```

The key is XOR'd into 64 zero-filled pads rather than hashing the key directly. This is required by the HMAC standard to ensure that the MAC is secure even if the hash has known weaknesses.

If `key_len < 64`, the remaining bytes of `k_ipad` and `k_opad` stay as 0x36 and 0x5C (not 0) — the XOR with the zero bytes is identity. The PSK in AegisCore is 16 bytes, so bytes 16–63 of both pads are `0x36` and `0x5C`.

---

## `HMAC_SHA256_Verify(key, key_len, data, data_len, expected, trunc_len)` → `bool`

```cpp
bool HMAC_SHA256_Verify(...) noexcept
{
    uint8_t computed[32];
    HMAC_SHA256(key, key_len, data, data_len, computed);

    // Constant-time comparison
    std::uint8_t diff = 0U;
    for (std::uint8_t i = 0U; i < trunc_len; ++i)
    {
        diff |= computed[i] ^ expected[i];
    }
    return diff == 0U;
}
```

**Constant-time comparison:** Instead of `if (computed[i] != expected[i]) return false;`, we accumulate all differences in `diff`. This ensures the function always takes the same time regardless of where the mismatch is.

**Why does timing matter?** A timing-variable comparison leaks information: if the function returns faster when the first byte is wrong vs. when all 8 are correct, an attacker can guess bytes one at a time. Constant-time comparison prevents this timing side-channel attack.

`trunc_len` is `kHmacTruncLen = 8` — we only compare the first 8 bytes of the 32-byte HMAC (the truncated version stored in the AC2 frame).
