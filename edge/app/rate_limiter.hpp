#ifndef AEGIS_EDGE_RATE_LIMITER_HPP
#define AEGIS_EDGE_RATE_LIMITER_HPP

#include <cstdint>

#include "domain.hpp"

namespace aegis::edge {

// Token-bucket rate limiter for incoming commands (SR-05).
// Capacities per IRS: CMD_SET_STATE 5 req/s burst 10;
//                     CMD_MANUAL_LOCK 2 req/s burst 3.
class RateLimiter
{
public:
    // Returns true if the command is within rate limits; consumes one token.
    // now_ms is the current uptime in milliseconds.
    [[nodiscard]] bool Allow(std::uint8_t cmd_id, std::uint32_t now_ms) noexcept;

private:
    struct Bucket
    {
        std::uint32_t last_refill_ms;
        std::uint32_t tokens;           // fixed-point: tokens * 1000
        std::uint32_t rate_per_sec;     // tokens added per second
        std::uint32_t burst_tokens;     // max tokens (burst * 1000)
    };

    static void Refill(Bucket& b, std::uint32_t now_ms) noexcept;

    // Bucket for CMD_SET_STATE (0x10): 5 req/s, burst 10.
    Bucket set_state_  = {0U, 10000U, 5000U, 10000U};
    // Bucket for CMD_MANUAL_LOCK (0x11): 2 req/s, burst 3.
    Bucket manual_lock_= {0U, 3000U,  2000U,  3000U};
};

} // namespace aegis::edge

#endif
