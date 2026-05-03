#include "fail_safe_supervisor.hpp"

namespace aegis::edge {

FailSafeSupervisor& FailSafeSupervisor::Instance() noexcept
{
    // Meyer's singleton. Initialized once in main() before tasks — safe with
    // -fno-threadsafe-statics because we never call this from concurrent contexts
    // at construction time (ARCH-01).
    static FailSafeSupervisor instance;
    return instance;
}

void FailSafeSupervisor::ReportEvent(FailSafeEvent /*event*/) noexcept
{
    triggered_ = true;
    degraded_  = true;
}

void FailSafeSupervisor::CheckHeartbeatTimeout(std::uint32_t now_ms) noexcept
{
    if (triggered_) { return; }

    if (last_hb_ms_ == 0U)
    {
        // First call before any gateway heartbeat — start the clock.
        last_hb_ms_ = now_ms;
        return;
    }

    const std::uint32_t elapsed = now_ms - last_hb_ms_;

    if (elapsed >= kFailSafeThresholdMs)
    {
        hb_miss_count_ = static_cast<std::uint8_t>(
            elapsed / 1000U < 255U ? elapsed / 1000U : 255U);
        ReportEvent(FailSafeEvent::HeartbeatLoss);
    }
    else if (elapsed >= kDegradedThresholdMs)
    {
        hb_miss_count_ = static_cast<std::uint8_t>(
            elapsed / 1000U < 255U ? elapsed / 1000U : 255U);
        degraded_ = true;
    }
    else
    {
        degraded_ = false;
    }
}

void FailSafeSupervisor::OnGatewayHeartbeatReceived(std::uint32_t now_ms) noexcept
{
    last_hb_ms_    = now_ms;
    hb_miss_count_ = 0U;
    degraded_      = false;
}

void FailSafeSupervisor::OnCrcError() noexcept
{
    if (triggered_) { return; }
    crc_err_count_ = crc_err_count_ + 1U;
    if (crc_err_count_ >= kMaxCrcErrors)
    {
        ReportEvent(FailSafeEvent::CrcErrorThreshold);
    }
}

void FailSafeSupervisor::OnHmacFailure() noexcept
{
    if (triggered_) { return; }
    hmac_fail_count_ = hmac_fail_count_ + 1U;
    if (hmac_fail_count_ >= kMaxHmacFailures)
    {
        ReportEvent(FailSafeEvent::HmacFailure);
    }
}

} // namespace aegis::edge
