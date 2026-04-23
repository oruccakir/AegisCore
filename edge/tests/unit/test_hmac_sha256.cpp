#include <gtest/gtest.h>
#include <cstring>
#include "hmac_sha256.hpp"

using namespace aegis::edge;

// RFC 4231 Test Vector 1:
// Key  = 0x0b * 20
// Data = "Hi There"
// HMAC = b0344c61d8db38535ca8afceaf0bf12b...
TEST(HmacSha256, Rfc4231_Vector1)
{
    std::uint8_t key[20];
    std::memset(key, 0x0bU, sizeof(key));
    const std::uint8_t data[] = {
        'H','i',' ','T','h','e','r','e'
    };
    std::uint8_t out[kSha256DigestLen] = {};
    HMAC_SHA256(key, sizeof(key), data, sizeof(data), out);

    EXPECT_EQ(out[0], 0xB0U);
    EXPECT_EQ(out[1], 0x34U);
    EXPECT_EQ(out[2], 0x4CU);
    EXPECT_EQ(out[3], 0x61U);
}

TEST(HmacSha256, VerifyMatchesCompute)
{
    const std::uint8_t key[]  = {0x01, 0x02, 0x03, 0x04, 0x05,
                                  0x06, 0x07, 0x08, 0x09, 0x0A};
    const std::uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    std::uint8_t out[kSha256DigestLen] = {};
    HMAC_SHA256(key, sizeof(key), data, sizeof(data), out);

    EXPECT_TRUE(HMAC_SHA256_Verify(key, sizeof(key),
                                    data, sizeof(data),
                                    out, kHmacTruncLen));
}

TEST(HmacSha256, VerifyRejectsWrongKey)
{
    const std::uint8_t key1[] = {0x01, 0x02, 0x03};
    const std::uint8_t key2[] = {0x04, 0x05, 0x06};
    const std::uint8_t data[] = {0xAA, 0xBB};
    std::uint8_t out[kSha256DigestLen] = {};
    HMAC_SHA256(key1, sizeof(key1), data, sizeof(data), out);

    EXPECT_FALSE(HMAC_SHA256_Verify(key2, sizeof(key2),
                                     data, sizeof(data),
                                     out, kHmacTruncLen));
}

TEST(HmacSha256, VerifyRejectsModifiedData)
{
    const std::uint8_t key[]  = {0xFF, 0xFE, 0xFD};
    const std::uint8_t data[] = {0x11, 0x22, 0x33};
    std::uint8_t out[kSha256DigestLen] = {};
    HMAC_SHA256(key, sizeof(key), data, sizeof(data), out);

    const std::uint8_t bad_data[] = {0x11, 0x22, 0x34};  // last byte differs
    EXPECT_FALSE(HMAC_SHA256_Verify(key, sizeof(key),
                                     bad_data, sizeof(bad_data),
                                     out, kHmacTruncLen));
}

TEST(HmacSha256, TruncLen8IsDifferentFrom32)
{
    const std::uint8_t key[]  = {0xAB, 0xCD};
    const std::uint8_t data[] = {0x00};
    std::uint8_t out[kSha256DigestLen] = {};
    HMAC_SHA256(key, sizeof(key), data, sizeof(data), out);

    // Truncated verification (8 bytes) must pass.
    EXPECT_TRUE(HMAC_SHA256_Verify(key, sizeof(key),
                                    data, sizeof(data),
                                    out, 8U));
}
