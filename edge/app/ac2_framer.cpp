#include "ac2_framer.hpp"

#include <cstring>

#include "crc16.hpp"
#include "hmac_sha256.hpp"

namespace aegis::edge {

// ---- AC2Framer -------------------------------------------------------------

static std::uint8_t BuildHeader(std::uint8_t cmd,
                                 std::uint8_t payload_len,
                                 std::uint32_t seq,
                                 std::uint8_t* buf) noexcept
{
    buf[0] = kAC2Sync;
    buf[1] = kAC2Version;
    buf[2] = static_cast<std::uint8_t>(seq         & 0xFFU);
    buf[3] = static_cast<std::uint8_t>((seq >>  8U) & 0xFFU);
    buf[4] = static_cast<std::uint8_t>((seq >> 16U) & 0xFFU);
    buf[5] = static_cast<std::uint8_t>((seq >> 24U) & 0xFFU);
    buf[6] = payload_len;
    buf[7] = cmd;
    return kAC2HeaderLen;
}

std::uint8_t AC2Framer::EncodeTelemetry(std::uint8_t cmd,
                                         const std::uint8_t* payload,
                                         std::uint8_t payload_len,
                                         std::uint32_t seq,
                                         std::uint8_t* out_buf) noexcept
{
    if (payload_len > kAC2MaxPayload) { payload_len = kAC2MaxPayload; }

    std::uint8_t pos = BuildHeader(cmd, payload_len, seq, out_buf);

    for (std::uint8_t i = 0U; i < payload_len; ++i) { out_buf[pos++] = payload[i]; }

    // HMAC = all zeros for telemetry direction (IRS §3.1).
    (void)std::memset(out_buf + pos, 0U, kAC2HmacLen);
    pos += kAC2HmacLen;

    const std::uint16_t crc = CRC16(out_buf, pos);
    out_buf[pos++] = static_cast<std::uint8_t>(crc & 0xFFU);
    out_buf[pos++] = static_cast<std::uint8_t>(crc >> 8U);
    return pos;
}

std::uint8_t AC2Framer::Encode(std::uint8_t cmd,
                                const std::uint8_t* payload,
                                std::uint8_t payload_len,
                                std::uint32_t seq,
                                const std::uint8_t* hmac_key,
                                std::uint8_t hmac_key_len,
                                std::uint8_t* out_buf) noexcept
{
    if (payload_len > kAC2MaxPayload) { payload_len = kAC2MaxPayload; }

    std::uint8_t pos = BuildHeader(cmd, payload_len, seq, out_buf);
    for (std::uint8_t i = 0U; i < payload_len; ++i) { out_buf[pos++] = payload[i]; }

    // HMAC over CMD_ID + PAYLOAD (per IRS §3.1).
    std::uint8_t body[1U + kAC2MaxPayload];
    body[0] = cmd;
    (void)std::memcpy(body + 1U, payload, payload_len);
    std::uint8_t digest[kSha256DigestLen];
    HMAC_SHA256(hmac_key, hmac_key_len,
                body, static_cast<std::uint16_t>(1U + payload_len),
                digest);
    (void)std::memcpy(out_buf + pos, digest, kAC2HmacLen);
    pos += kAC2HmacLen;

    const std::uint16_t crc = CRC16(out_buf, pos);
    out_buf[pos++] = static_cast<std::uint8_t>(crc & 0xFFU);
    out_buf[pos++] = static_cast<std::uint8_t>(crc >> 8U);
    return pos;
}

// ---- AC2Parser -------------------------------------------------------------

void AC2Parser::SetCallback(FrameCallback cb, void* ctx) noexcept
{
    cb_     = cb;
    cb_ctx_ = ctx;
}

void AC2Parser::Reset() noexcept
{
    state_       = State::WaitSync;
    payload_idx_ = 0U;
    hmac_idx_    = 0U;
    raw_len_     = 0U;
    rx_crc_lo_   = 0U;
    (void)std::memset(&frame_, 0U, sizeof(frame_));
}

void AC2Parser::Dispatch() noexcept
{
    // CRC covers everything up to (but not including) the two CRC bytes.
    const std::uint16_t expected_crc = CRC16(raw_, static_cast<std::uint8_t>(raw_len_));
    const std::uint16_t received_crc =
        static_cast<std::uint16_t>(rx_crc_lo_) |
        (static_cast<std::uint16_t>(raw_[raw_len_]) << 8U);

    if (expected_crc != received_crc)
    {
        ++crc_errors_;
        Reset();
        return;
    }

    if (cb_ != nullptr) { cb_(frame_, cb_ctx_); }
    Reset();
}

void AC2Parser::Feed(std::uint8_t byte) noexcept
{
    switch (state_)
    {
    case State::WaitSync:
        if (byte == kAC2Sync)
        {
            Reset();
            raw_[raw_len_++] = byte;
            state_ = State::WaitVersion;
        }
        break;

    case State::WaitVersion:
        raw_[raw_len_++] = byte;
        if (byte != kAC2Version) { Reset(); break; }
        state_ = State::WaitSeq0;
        break;

    case State::WaitSeq0:
        raw_[raw_len_++] = byte;
        frame_.seq  = static_cast<std::uint32_t>(byte);
        state_ = State::WaitSeq1;
        break;

    case State::WaitSeq1:
        raw_[raw_len_++] = byte;
        frame_.seq |= static_cast<std::uint32_t>(byte) << 8U;
        state_ = State::WaitSeq2;
        break;

    case State::WaitSeq2:
        raw_[raw_len_++] = byte;
        frame_.seq |= static_cast<std::uint32_t>(byte) << 16U;
        state_ = State::WaitSeq3;
        break;

    case State::WaitSeq3:
        raw_[raw_len_++] = byte;
        frame_.seq |= static_cast<std::uint32_t>(byte) << 24U;
        state_ = State::WaitLength;
        break;

    case State::WaitLength:
        raw_[raw_len_++] = byte;
        if (byte > kAC2MaxPayload) { Reset(); break; } // SR-08: drop oversized
        frame_.payload_len = byte;
        state_ = State::WaitCmd;
        break;

    case State::WaitCmd:
        raw_[raw_len_++] = byte;
        frame_.cmd   = byte;
        payload_idx_ = 0U;
        state_ = (frame_.payload_len > 0U) ? State::WaitPayload : State::WaitHmac;
        break;

    case State::WaitPayload:
        raw_[raw_len_++] = byte;
        frame_.payload[payload_idx_++] = byte;
        if (payload_idx_ == frame_.payload_len)
        {
            hmac_idx_ = 0U;
            state_    = State::WaitHmac;
        }
        break;

    case State::WaitHmac:
        raw_[raw_len_++] = byte;
        frame_.hmac[hmac_idx_++] = byte;
        if (hmac_idx_ == kAC2HmacLen) { state_ = State::WaitCrc0; }
        break;

    case State::WaitCrc0:
        rx_crc_lo_ = byte;
        state_ = State::WaitCrc1;
        break;

    case State::WaitCrc1:
        // Store CRC-HI temporarily after raw[] for Dispatch() to read.
        raw_[raw_len_] = byte;
        Dispatch();
        break;
    }
}

} // namespace aegis::edge
