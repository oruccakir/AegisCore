#include <gtest/gtest.h>
#include "rate_limiter.hpp"
#include "telemetry.hpp"

using namespace aegis::edge;

TEST(RateLimiter, ManualLockAllowsBurst)
{
    RateLimiter rl;
    // Initial tokens = burst = 3.
    EXPECT_TRUE(rl.Allow(CmdId::kManualLock, 0U));
    EXPECT_TRUE(rl.Allow(CmdId::kManualLock, 0U));
    EXPECT_TRUE(rl.Allow(CmdId::kManualLock, 0U));
    EXPECT_FALSE(rl.Allow(CmdId::kManualLock, 0U));
}

TEST(RateLimiter, UnknownCmdAlwaysAllowed)
{
    RateLimiter rl;
    // Commands not rate-limited should pass through freely.
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_TRUE(rl.Allow(CmdId::kHeartbeat, 0U));
    }
}
