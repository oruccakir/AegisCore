#include "hmac_sha256.hpp"

#include <cstring>

namespace aegis::edge {

namespace {

// SHA-256 round constants.
constexpr std::uint32_t kK[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

// SHA-256 initial hash values.
constexpr std::uint32_t kInitState[8] = {
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
};

struct Sha256Ctx
{
    std::uint32_t state[8];
    std::uint32_t lo;   // bit count low
    std::uint32_t hi;   // bit count high
    std::uint8_t  buf[64];
    std::uint32_t buf_len;
};

constexpr std::uint32_t Rotr(std::uint32_t x, std::uint32_t n) noexcept
{
    return (x >> n) | (x << (32U - n));
}

constexpr std::uint32_t Ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept
{
    return (x & y) ^ (~x & z);
}

constexpr std::uint32_t Maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept
{
    return (x & y) ^ (x & z) ^ (y & z);
}

constexpr std::uint32_t Sigma0(std::uint32_t x) noexcept
{
    return Rotr(x, 2U) ^ Rotr(x, 13U) ^ Rotr(x, 22U);
}

constexpr std::uint32_t Sigma1(std::uint32_t x) noexcept
{
    return Rotr(x, 6U) ^ Rotr(x, 11U) ^ Rotr(x, 25U);
}

constexpr std::uint32_t sigma0(std::uint32_t x) noexcept
{
    return Rotr(x, 7U) ^ Rotr(x, 18U) ^ (x >> 3U);
}

constexpr std::uint32_t sigma1(std::uint32_t x) noexcept
{
    return Rotr(x, 17U) ^ Rotr(x, 19U) ^ (x >> 10U);
}

void Sha256Transform(Sha256Ctx& ctx, const std::uint8_t* block) noexcept
{
    std::uint32_t w[64];
    for (std::uint32_t i = 0U; i < 16U; ++i)
    {
        w[i] = (static_cast<std::uint32_t>(block[i * 4U + 0U]) << 24U)
             | (static_cast<std::uint32_t>(block[i * 4U + 1U]) << 16U)
             | (static_cast<std::uint32_t>(block[i * 4U + 2U]) <<  8U)
             |  static_cast<std::uint32_t>(block[i * 4U + 3U]);
    }
    for (std::uint32_t i = 16U; i < 64U; ++i)
    {
        w[i] = sigma1(w[i - 2U]) + w[i - 7U] + sigma0(w[i - 15U]) + w[i - 16U];
    }

    std::uint32_t a = ctx.state[0], b = ctx.state[1];
    std::uint32_t c = ctx.state[2], d = ctx.state[3];
    std::uint32_t e = ctx.state[4], f = ctx.state[5];
    std::uint32_t g = ctx.state[6], h = ctx.state[7];

    for (std::uint32_t i = 0U; i < 64U; ++i)
    {
        const std::uint32_t t1 = h + Sigma1(e) + Ch(e, f, g) + kK[i] + w[i];
        const std::uint32_t t2 = Sigma0(a) + Maj(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx.state[0] += a; ctx.state[1] += b;
    ctx.state[2] += c; ctx.state[3] += d;
    ctx.state[4] += e; ctx.state[5] += f;
    ctx.state[6] += g; ctx.state[7] += h;
}

void Sha256Init(Sha256Ctx& ctx) noexcept
{
    for (std::uint32_t i = 0U; i < 8U; ++i) { ctx.state[i] = kInitState[i]; }
    ctx.lo      = 0U;
    ctx.hi      = 0U;
    ctx.buf_len = 0U;
}

void Sha256Update(Sha256Ctx& ctx, const std::uint8_t* data, std::uint32_t len) noexcept
{
    // Update bit count.
    const std::uint32_t bits_lo_prev = ctx.lo;
    ctx.lo += len * 8U;
    if (ctx.lo < bits_lo_prev) { ++ctx.hi; }
    ctx.hi += len >> 29U;

    std::uint32_t i = 0U;
    if (ctx.buf_len > 0U)
    {
        const std::uint32_t space = 64U - ctx.buf_len;
        const std::uint32_t fill  = (len < space) ? len : space;
        (void)std::memcpy(ctx.buf + ctx.buf_len, data, fill);
        ctx.buf_len += fill;
        i += fill;
        if (ctx.buf_len == 64U)
        {
            Sha256Transform(ctx, ctx.buf);
            ctx.buf_len = 0U;
        }
    }
    while (i + 64U <= len)
    {
        Sha256Transform(ctx, data + i);
        i += 64U;
    }
    if (i < len)
    {
        const std::uint32_t tail = len - i;
        (void)std::memcpy(ctx.buf, data + i, tail);
        ctx.buf_len = tail;
    }
}

void Sha256Final(Sha256Ctx& ctx, std::uint8_t out[32]) noexcept
{
    // Append 0x80 padding byte.
    ctx.buf[ctx.buf_len++] = 0x80U;

    // If not enough room for length, pad to end of block and process.
    if (ctx.buf_len > 56U)
    {
        (void)std::memset(ctx.buf + ctx.buf_len, 0U, 64U - ctx.buf_len);
        Sha256Transform(ctx, ctx.buf);
        ctx.buf_len = 0U;
    }

    (void)std::memset(ctx.buf + ctx.buf_len, 0U, 56U - ctx.buf_len);

    // Append bit length as 64-bit big-endian.
    ctx.buf[56] = static_cast<std::uint8_t>(ctx.hi >> 24U);
    ctx.buf[57] = static_cast<std::uint8_t>(ctx.hi >> 16U);
    ctx.buf[58] = static_cast<std::uint8_t>(ctx.hi >>  8U);
    ctx.buf[59] = static_cast<std::uint8_t>(ctx.hi);
    ctx.buf[60] = static_cast<std::uint8_t>(ctx.lo >> 24U);
    ctx.buf[61] = static_cast<std::uint8_t>(ctx.lo >> 16U);
    ctx.buf[62] = static_cast<std::uint8_t>(ctx.lo >>  8U);
    ctx.buf[63] = static_cast<std::uint8_t>(ctx.lo);
    Sha256Transform(ctx, ctx.buf);

    for (std::uint32_t i = 0U; i < 8U; ++i)
    {
        out[i * 4U + 0U] = static_cast<std::uint8_t>(ctx.state[i] >> 24U);
        out[i * 4U + 1U] = static_cast<std::uint8_t>(ctx.state[i] >> 16U);
        out[i * 4U + 2U] = static_cast<std::uint8_t>(ctx.state[i] >>  8U);
        out[i * 4U + 3U] = static_cast<std::uint8_t>(ctx.state[i]);
    }
}

} // namespace

void HMAC_SHA256(const std::uint8_t* key,  std::uint8_t key_len,
                 const std::uint8_t* data, std::uint16_t data_len,
                 std::uint8_t out[kSha256DigestLen]) noexcept
{
    // Prepare padded key (key_len <= 64 assumed — PSK is 32 bytes).
    std::uint8_t k_ipad[64];
    std::uint8_t k_opad[64];
    (void)std::memset(k_ipad, 0x36U, 64U);
    (void)std::memset(k_opad, 0x5CU, 64U);
    for (std::uint8_t i = 0U; i < key_len; ++i)
    {
        k_ipad[i] ^= key[i];
        k_opad[i] ^= key[i];
    }

    // Inner hash: SHA-256(k_ipad || data).
    std::uint8_t inner[32];
    Sha256Ctx ctx{};
    Sha256Init(ctx);
    Sha256Update(ctx, k_ipad, 64U);
    Sha256Update(ctx, data, static_cast<std::uint32_t>(data_len));
    Sha256Final(ctx, inner);

    // Outer hash: SHA-256(k_opad || inner).
    Sha256Init(ctx);
    Sha256Update(ctx, k_opad, 64U);
    Sha256Update(ctx, inner, 32U);
    Sha256Final(ctx, out);
}

bool HMAC_SHA256_Verify(const std::uint8_t* key,      std::uint8_t key_len,
                         const std::uint8_t* data,     std::uint16_t data_len,
                         const std::uint8_t* expected, std::uint8_t trunc_len) noexcept
{
    std::uint8_t computed[kSha256DigestLen];
    HMAC_SHA256(key, key_len, data, data_len, computed);

    // Constant-time comparison of first trunc_len bytes.
    std::uint8_t diff = 0U;
    for (std::uint8_t i = 0U; i < trunc_len; ++i)
    {
        diff |= computed[i] ^ expected[i];
    }
    return diff == 0U;
}

} // namespace aegis::edge
