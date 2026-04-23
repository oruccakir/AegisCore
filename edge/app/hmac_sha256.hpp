#ifndef AEGIS_EDGE_HMAC_SHA256_HPP
#define AEGIS_EDGE_HMAC_SHA256_HPP

#include <cstdint>

namespace aegis::edge {

inline constexpr std::uint8_t kSha256DigestLen = 32U;
inline constexpr std::uint8_t kHmacTruncLen    = 8U;   // AC2 uses first 8 bytes

// Compute HMAC-SHA-256 over data[0..data_len-1] with key[0..key_len-1].
// Writes kSha256DigestLen bytes to out. key_len must be <= 64 (block size).
void HMAC_SHA256(const std::uint8_t* key,  std::uint8_t key_len,
                 const std::uint8_t* data, std::uint16_t data_len,
                 std::uint8_t out[kSha256DigestLen]) noexcept;

// Verify: compute HMAC and compare first trunc_len bytes against expected.
// Constant-time comparison to resist timing attacks.
[[nodiscard]] bool HMAC_SHA256_Verify(
    const std::uint8_t* key,      std::uint8_t key_len,
    const std::uint8_t* data,     std::uint16_t data_len,
    const std::uint8_t* expected, std::uint8_t trunc_len) noexcept;

} // namespace aegis::edge

#endif
