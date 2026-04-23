#ifndef AEGIS_EDGE_TELEMETRY_HPP
#define AEGIS_EDGE_TELEMETRY_HPP

#include <cstdint>

#include "domain.hpp"

namespace aegis::edge {

// AC2 command IDs matching docs/IRS/ac2-telemetry-irs.tex §4.
namespace CmdId {
    inline constexpr std::uint8_t kGetVersion    = 0x01U;
    inline constexpr std::uint8_t kVersionReport  = 0x02U;
    inline constexpr std::uint8_t kSetState       = 0x10U;
    inline constexpr std::uint8_t kManualLock     = 0x11U;
    inline constexpr std::uint8_t kResetAck       = 0x12U;
    inline constexpr std::uint8_t kReportState    = 0x20U;
    inline constexpr std::uint8_t kTelemetryTick  = 0x21U;
    inline constexpr std::uint8_t kFaultReport    = 0x30U;
    inline constexpr std::uint8_t kAuditEvent     = 0x31U;
    inline constexpr std::uint8_t kAck            = 0x80U;
    inline constexpr std::uint8_t kNack           = 0x81U;
    inline constexpr std::uint8_t kHeartbeat      = 0x99U;
}

// NACK error codes (IRS §4).
namespace ErrCode {
    inline constexpr std::uint8_t kInvalidCmd        = 0x01U;
    inline constexpr std::uint8_t kInvalidPayload    = 0x02U;
    inline constexpr std::uint8_t kInvalidTransition = 0x03U;
    inline constexpr std::uint8_t kAuthFail          = 0x04U;
    inline constexpr std::uint8_t kReplay            = 0x05U;
    inline constexpr std::uint8_t kRateLimited       = 0x06U;
    inline constexpr std::uint8_t kFailSafeLock      = 0x07U;
    inline constexpr std::uint8_t kBusy              = 0x08U;
}

// Fault codes reported via CMD_FAULT_REPORT.
namespace FaultCode {
    inline constexpr std::uint8_t kStackOverflow = 0x01U;
    inline constexpr std::uint8_t kHardFault     = 0x02U;
    inline constexpr std::uint8_t kWatchdogReset = 0x03U;
    inline constexpr std::uint8_t kPostFail      = 0x04U;
    inline constexpr std::uint8_t kCrcThreshold  = 0x05U;
    inline constexpr std::uint8_t kHmacFail      = 0x06U;
    inline constexpr std::uint8_t kHeartbeatLoss = 0x07U;
}

// Payload structs — all fields little-endian.

struct __attribute__((packed)) PayloadReportState
{
    std::uint8_t  state;
    std::uint8_t  prev_state;
    std::uint32_t uptime_ms;
};
static_assert(sizeof(PayloadReportState) == 6U);

struct __attribute__((packed)) PayloadTelemetryTick
{
    std::uint8_t  state;
    std::uint16_t cpu_load_x10;      // CPU usage * 10 (e.g. 153 = 15.3 %)
    std::uint16_t free_stack_min_words;
    std::uint8_t  hb_miss_count;
};
static_assert(sizeof(PayloadTelemetryTick) == 6U);

struct __attribute__((packed)) PayloadFaultReport
{
    std::uint8_t  fault_code;
    std::uint16_t ctx;
    std::uint32_t reset_reason_bits;
};
static_assert(sizeof(PayloadFaultReport) == 7U);

struct __attribute__((packed)) PayloadHeartbeatTx
{
    std::uint32_t uptime_ms;
};
static_assert(sizeof(PayloadHeartbeatTx) == 4U);

struct __attribute__((packed)) PayloadVersionReport
{
    std::uint8_t  major;
    std::uint8_t  minor;
    std::uint8_t  patch;
    std::uint8_t  git_sha[8];
    std::uint32_t build_ts;
};
static_assert(sizeof(PayloadVersionReport) == 15U);

struct __attribute__((packed)) PayloadAck
{
    std::uint32_t echoed_seq;
};
static_assert(sizeof(PayloadAck) == 4U);

struct __attribute__((packed)) PayloadNack
{
    std::uint32_t echoed_seq;
    std::uint8_t  err_code;
};
static_assert(sizeof(PayloadNack) == 5U);

} // namespace aegis::edge

#endif
