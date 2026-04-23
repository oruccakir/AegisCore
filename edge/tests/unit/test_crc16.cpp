#include <gtest/gtest.h>
#include "crc16.hpp"

using namespace aegis::edge;

TEST(Crc16, EmptyBuffer)
{
    const std::uint8_t dummy = 0U;
    EXPECT_EQ(CRC16(&dummy, 0U), 0xFFFFU);
}

TEST(Crc16, KnownVector_123456789)
{
    // Standard CCITT-FALSE check value for "123456789".
    const std::uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
    EXPECT_EQ(CRC16(data, sizeof(data)), 0x29B1U);
}

TEST(Crc16, SingleZeroByte)
{
    const std::uint8_t data[] = {0x00U};
    EXPECT_EQ(CRC16(data, 1U), 0xE1F0U);
}

TEST(Crc16, Idempotent)
{
    const std::uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    EXPECT_EQ(CRC16(data, sizeof(data)), CRC16(data, sizeof(data)));
}

TEST(Crc16, SingleByteAA)
{
    // SYNC byte 0xAA used in AC2 frames.
    const std::uint8_t data[] = {0xAAU};
    const std::uint16_t crc1 = CRC16(data, 1U);
    EXPECT_EQ(CRC16(data, 1U), crc1);  // deterministic
}
