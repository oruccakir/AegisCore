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
    inline constexpr std::uint8_t kTelemetryTick   = 0x21U;
    inline constexpr std::uint8_t kTaskList        = 0x22U;
    inline constexpr std::uint8_t kFaultReport     = 0x30U;
    inline constexpr std::uint8_t kAuditEvent      = 0x31U;
    inline constexpr std::uint8_t kCreateTask      = 0x50U;
    inline constexpr std::uint8_t kDeleteTask      = 0x51U;
    inline constexpr std::uint8_t kDetectionResult = 0x60U;
    inline constexpr std::uint8_t kAck             = 0x80U;
    inline constexpr std::uint8_t kNack            = 0x81U;
    inline constexpr std::uint8_t kHeartbeat       = 0x99U;
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
    std::uint16_t cpu_load_x10;          // CPU usage * 10 (e.g. 153 = 15.3 %)
    std::uint16_t stack_uart_rx;         // free words — UartRx task
    std::uint16_t stack_state_core;      // free words — StateCore task
    std::uint16_t stack_tel_tx;          // free words — TelTx task
    std::uint16_t stack_heartbeat;       // free words — Heartbeat task
    std::uint8_t  hb_miss_count;
};
static_assert(sizeof(PayloadTelemetryTick) == 12U);

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

// Per-task entry packed into kTaskList frames (14 bytes, 9 entries fit in 127 ≤ 128 bytes).
struct __attribute__((packed)) PackedTaskEntry
{
    char          name[8];
    std::uint8_t  state;           // eTaskState value
    std::uint8_t  priority;
    std::uint16_t stack_watermark; // high-water-mark in words
    std::uint8_t  cpu_load;        // 0–100 percent
    std::uint8_t  task_id;         // bit7=1 → user task, bits[2:0] = slot index
};
static_assert(sizeof(PackedTaskEntry) == 14U);

struct __attribute__((packed)) PayloadCreateTask
{
    std::uint8_t task_type; // 0=BLINK, 1=COUNTER, 2=LOAD
    std::uint8_t param;
};
static_assert(sizeof(PayloadCreateTask) == 2U);

struct __attribute__((packed)) PayloadDeleteTask
{
    std::uint8_t slot_index;
};
static_assert(sizeof(PayloadDeleteTask) == 1U);

struct __attribute__((packed)) PayloadDetectionResult
{
    std::uint8_t class_id;       // 0=no detection, 1=person
    std::uint8_t confidence_pct; // floor(confidence * 100)
};
static_assert(sizeof(PayloadDetectionResult) == 2U);

} // namespace aegis::edge

#endif
