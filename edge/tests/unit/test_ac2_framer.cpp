#include <gtest/gtest.h>
#include <cstring>
#include "ac2_framer.hpp"
#include "crc16.hpp"

using namespace aegis::edge;

// ---- Encoder tests ----------------------------------------------------------

TEST(Ac2Framer, EncodeTelemetry_FrameStructure)
{
    const std::uint8_t payload[] = {0x01U, 0x02U, 0x03U};
    std::uint8_t buf[kAC2MaxFrame] = {};
    const std::uint8_t len =
        AC2Framer::EncodeTelemetry(0x21U, payload, sizeof(payload), 42U, buf);

    // Total = 8 header + 3 payload + 8 HMAC + 2 CRC = 21 bytes.
    EXPECT_EQ(len, 21U);
    EXPECT_EQ(buf[0], kAC2Sync);
    EXPECT_EQ(buf[1], kAC2Version);
    // SEQ = 42 little-endian.
    EXPECT_EQ(buf[2], 42U);
    EXPECT_EQ(buf[3], 0U);
    EXPECT_EQ(buf[4], 0U);
    EXPECT_EQ(buf[5], 0U);
    EXPECT_EQ(buf[6], 3U);    // LENGTH
    EXPECT_EQ(buf[7], 0x21U); // CMD
    EXPECT_EQ(buf[8], 0x01U);
    EXPECT_EQ(buf[9], 0x02U);
    EXPECT_EQ(buf[10], 0x03U);
}

TEST(Ac2Framer, EncodeTelemetry_HmacIsAllZeros)
{
    const std::uint8_t payload[] = {0xAAU};
    std::uint8_t buf[kAC2MaxFrame] = {};
    const std::uint8_t len =
        AC2Framer::EncodeTelemetry(0x20U, payload, 1U, 0U, buf);
    (void)len;

    // HMAC starts at offset 8+1 = 9.
    for (std::uint8_t i = 9U; i < 9U + kAC2HmacLen; ++i)
    {
        EXPECT_EQ(buf[i], 0U) << "HMAC byte " << i << " must be zero";
    }
}

TEST(Ac2Framer, EncodeTelemetry_CrcValid)
{
    const std::uint8_t payload[] = {0x01U};
    std::uint8_t buf[kAC2MaxFrame] = {};
    const std::uint8_t flen =
        AC2Framer::EncodeTelemetry(0x99U, payload, 1U, 0U, buf);

    // CRC covers everything except the last 2 bytes.
    const std::uint16_t expected =
        CRC16(buf, static_cast<std::uint8_t>(flen - 2U));
    const std::uint16_t received =
        static_cast<std::uint16_t>(buf[flen - 2U]) |
        (static_cast<std::uint16_t>(buf[flen - 1U]) << 8U);
    EXPECT_EQ(expected, received);
}

TEST(Ac2Framer, EncodeWithHmac_CrcValid)
{
    const std::uint8_t key[]     = {0x01U, 0x02U, 0x03U};
    const std::uint8_t payload[] = {0xDEU, 0xADU};
    std::uint8_t buf[kAC2MaxFrame] = {};
    const std::uint8_t flen =
        AC2Framer::Encode(0x10U, payload, sizeof(payload),
                           1U, key, sizeof(key), buf);

    const std::uint16_t expected =
        CRC16(buf, static_cast<std::uint8_t>(flen - 2U));
    const std::uint16_t received =
        static_cast<std::uint16_t>(buf[flen - 2U]) |
        (static_cast<std::uint16_t>(buf[flen - 1U]) << 8U);
    EXPECT_EQ(expected, received);
}

TEST(Ac2Framer, EncodeTelemetry_EmptyPayload)
{
    std::uint8_t buf[kAC2MaxFrame] = {};
    const std::uint8_t len =
        AC2Framer::EncodeTelemetry(0x80U, nullptr, 0U, 0U, buf);
    // 8 header + 0 payload + 8 HMAC + 2 CRC = 18 bytes.
    EXPECT_EQ(len, 18U);
    EXPECT_EQ(buf[6], 0U); // LENGTH = 0
}

// ---- Parser tests -----------------------------------------------------------

TEST(Ac2Parser, ParsesEncodedTelemetryFrame)
{
    const std::uint8_t payload[] = {0x01U, 0x02U};
    std::uint8_t buf[kAC2MaxFrame] = {};
    const std::uint8_t flen =
        AC2Framer::EncodeTelemetry(0x21U, payload, sizeof(payload), 7U, buf);

    AC2Parser parser;
    struct Ctx { AC2Frame frame; bool called; } capture = {};
    parser.SetCallback(
        [](const AC2Frame& f, void* c) {
            auto* ctx = static_cast<Ctx*>(c);
            ctx->frame  = f;
            ctx->called = true;
        },
        &capture);

    for (std::uint8_t i = 0U; i < flen; ++i) { parser.Feed(buf[i]); }

    EXPECT_TRUE(capture.called);
    EXPECT_EQ(capture.frame.seq, 7U);
    EXPECT_EQ(capture.frame.cmd, 0x21U);
    EXPECT_EQ(capture.frame.payload_len, 2U);
    EXPECT_EQ(capture.frame.payload[0], 0x01U);
    EXPECT_EQ(capture.frame.payload[1], 0x02U);
}

TEST(Ac2Parser, RejectsOversizedLength)
{
    AC2Parser parser;
    bool called = false;
    parser.SetCallback([](const AC2Frame&, void* c) {
        *static_cast<bool*>(c) = true;
    }, &called);

    // Inject a frame with LENGTH > kAC2MaxPayload (48).
    const std::uint8_t bad_frame[] = {
        kAC2Sync, kAC2Version,
        0x01U, 0x00U, 0x00U, 0x00U,  // SEQ = 1
        0x3FU,                         // LENGTH = 63 > 48 → drop (SR-08)
        0x10U                          // CMD
    };
    for (auto b : bad_frame) { parser.Feed(b); }
    EXPECT_FALSE(called);
}

TEST(Ac2Parser, CountsCrcErrors)
{
    AC2Parser parser;
    parser.SetCallback(nullptr, nullptr);

    // Build a valid frame then corrupt the CRC.
    const std::uint8_t payload[] = {0x01U};
    std::uint8_t buf[kAC2MaxFrame] = {};
    const std::uint8_t flen =
        AC2Framer::EncodeTelemetry(0x21U, payload, 1U, 0U, buf);

    buf[flen - 1U] ^= 0xFFU; // corrupt CRC high byte

    for (std::uint8_t i = 0U; i < flen; ++i) { parser.Feed(buf[i]); }

    EXPECT_EQ(parser.CrcErrorCount(), 1U);
}

TEST(Ac2Parser, HandlesGarbageBeforeSync)
{
    AC2Parser parser;
    bool called = false;
    parser.SetCallback([](const AC2Frame&, void* c) {
        *static_cast<bool*>(c) = true;
    }, &called);

    // Garbage bytes followed by a valid frame.
    const std::uint8_t payload[] = {0xBBU};
    std::uint8_t buf[kAC2MaxFrame] = {};
    const std::uint8_t flen =
        AC2Framer::EncodeTelemetry(0x20U, payload, 1U, 0U, buf);

    const std::uint8_t garbage[] = {0x00U, 0x11U, 0x22U, 0x33U};
    for (auto b : garbage) { parser.Feed(b); }
    for (std::uint8_t i = 0U; i < flen; ++i) { parser.Feed(buf[i]); }

    EXPECT_TRUE(called);
    EXPECT_EQ(parser.CrcErrorCount(), 0U);
}
