#include "rate_limiter.hpp"

#include "telemetry.hpp"

namespace aegis::edge {

void RateLimiter::Refill(Bucket& b, std::uint32_t now_ms) noexcept
{
    const std::uint32_t elapsed_ms = now_ms - b.last_refill_ms;
    if (elapsed_ms == 0U) { return; }

    // Add (rate_per_sec * elapsed_ms / 1000) tokens, capped at burst.
    const std::uint32_t added = b.rate_per_sec * elapsed_ms / 1000U;
    b.tokens = (b.tokens + added > b.burst_tokens) ? b.burst_tokens
                                                    : b.tokens + added;
    b.last_refill_ms = now_ms;
}

bool RateLimiter::Allow(std::uint8_t cmd_id, std::uint32_t now_ms) noexcept
{
    Bucket* bucket = nullptr;

    if (cmd_id == CmdId::kSetState)
    {
        bucket = &set_state_;
    }
    else if (cmd_id == CmdId::kManualLock)
    {
        bucket = &manual_lock_;
    }
    else
    {
        return true; // no rate limit on other commands
    }

    Refill(*bucket, now_ms);

    if (bucket->tokens < 1000U) // 1000 = one token in fixed-point
    {
        return false;
    }

    bucket->tokens -= 1000U;
    return true;
}

} // namespace aegis::edge
