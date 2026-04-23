#ifndef AEGIS_EDGE_AC2_FRAMER_HPP
#define AEGIS_EDGE_AC2_FRAMER_HPP

#include <cstdint>

namespace aegis::edge {

inline constexpr std::uint8_t kAC2Sync        = 0xAAU;
inline constexpr std::uint8_t kAC2Version     = 0x02U;
inline constexpr std::uint8_t kAC2MaxPayload  = 48U;
inline constexpr std::uint8_t kAC2MaxFrame    = 66U; // 18-byte overhead + 48 payload
inline constexpr std::uint8_t kAC2HmacLen     = 8U;
inline constexpr std::uint8_t kAC2HeaderLen   = 8U;  // SYNC+VER+SEQ(4)+LEN+CMD
inline constexpr std::uint8_t kAC2OverheadLen = 18U; // header + HMAC + CRC

// Received / decoded frame (passed to callback).
struct AC2Frame
{
    std::uint32_t seq;
    std::uint8_t  cmd;
    std::uint8_t  payload[kAC2MaxPayload];
    std::uint8_t  payload_len;
    std::uint8_t  hmac[kAC2HmacLen];
};

// ---- Encoder ---------------------------------------------------------------

class AC2Framer
{
public:
    // Encode a telemetry frame (Edge -> Host). HMAC bytes are all zero.
    // Returns total frame length written to out_buf.
    static std::uint8_t EncodeTelemetry(std::uint8_t cmd,
                                        const std::uint8_t* payload,
                                        std::uint8_t payload_len,
                                        std::uint32_t seq,
                                        std::uint8_t* out_buf) noexcept;

    // Encode a frame with HMAC (for command responses or future Host->Edge use).
    // hmac_key / hmac_key_len identify the PSK.
    static std::uint8_t Encode(std::uint8_t cmd,
                                const std::uint8_t* payload,
                                std::uint8_t payload_len,
                                std::uint32_t seq,
                                const std::uint8_t* hmac_key,
                                std::uint8_t hmac_key_len,
                                std::uint8_t* out_buf) noexcept;
};

// ---- Parser ----------------------------------------------------------------

class AC2Parser
{
public:
    using FrameCallback = void (*)(const AC2Frame&, void* ctx);

    void SetCallback(FrameCallback cb, void* ctx) noexcept;
    void Feed(std::uint8_t byte) noexcept;
    void Reset() noexcept;

    [[nodiscard]] std::uint32_t CrcErrorCount() const noexcept { return crc_errors_; }

private:
    enum class State : std::uint8_t
    {
        WaitSync, WaitVersion,
        WaitSeq0, WaitSeq1, WaitSeq2, WaitSeq3,
        WaitLength, WaitCmd,
        WaitPayload, WaitHmac, WaitCrc0, WaitCrc1
    };

    void Dispatch() noexcept;

    State    state_       = State::WaitSync;
    AC2Frame frame_       = {};
    std::uint8_t payload_idx_ = 0U;
    std::uint8_t hmac_idx_    = 0U;

    // Raw byte accumulator for CRC verification.
    std::uint8_t  raw_[kAC2MaxFrame] = {};
    std::uint8_t  raw_len_           = 0U;
    std::uint8_t  rx_crc_lo_         = 0U;

    FrameCallback cb_     = nullptr;
    void*         cb_ctx_ = nullptr;
    std::uint32_t crc_errors_ = 0U;
};

} // namespace aegis::edge

#endif
