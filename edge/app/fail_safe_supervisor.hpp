#ifndef AEGIS_EDGE_FAIL_SAFE_SUPERVISOR_HPP
#define AEGIS_EDGE_FAIL_SAFE_SUPERVISOR_HPP

#include <cstdint>

namespace aegis::edge {

enum class FailSafeEvent : std::uint8_t
{
    StackOverflow,
    HardFault,
    HeartbeatLoss,
    CrcErrorThreshold,
    HmacFailure,
    PostFailure,
    ExternalTrigger,
};

// Singleton — call Instance() once in main() before starting tasks (SAF-02).
// Tracks heartbeat timeouts, CRC/HMAC error counters, and fault events.
// Sets volatile flags that StateMachineTask polls each cycle.
class FailSafeSupervisor
{
public:
    static FailSafeSupervisor& Instance() noexcept;

    // May be called from ISR or task context.
    void ReportEvent(FailSafeEvent event) noexcept;

    // Called by HeartbeatTask each second with current uptime.
    void CheckHeartbeatTimeout(std::uint32_t now_ms) noexcept;

    // Called by UartRxTask when a valid heartbeat frame arrives from gateway.
    void OnGatewayHeartbeatReceived(std::uint32_t now_ms) noexcept;

    // Called by AC2Parser on each CRC mismatch.
    void OnCrcError() noexcept;

    // Called by HMAC verifier on each auth failure.
    void OnHmacFailure() noexcept;

    [[nodiscard]] bool IsTriggered() const noexcept { return triggered_; }
    [[nodiscard]] bool IsDegraded()  const noexcept { return degraded_; }
    [[nodiscard]] std::uint8_t HeartbeatMissCount() const noexcept { return hb_miss_count_; }

private:
    FailSafeSupervisor() noexcept = default;

    static constexpr std::uint32_t kDegradedThresholdMs  = 3000U;
    static constexpr std::uint32_t kFailSafeThresholdMs  = 5000U;
    static constexpr std::uint8_t  kMaxCrcErrors         = 10U;
    static constexpr std::uint8_t  kMaxHmacFailures      = 5U;

    volatile bool         triggered_       = false;
    volatile bool         degraded_        = false;
    volatile std::uint32_t last_hb_ms_     = 0U;  // 0 = not yet received
    volatile std::uint8_t  hb_miss_count_  = 0U;
    volatile std::uint8_t  crc_err_count_  = 0U;
    volatile std::uint8_t  hmac_fail_count_= 0U;
};

} // namespace aegis::edge

#endif
