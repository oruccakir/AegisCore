#include <gtest/gtest.h>
#include "rate_limiter.hpp"
#include "telemetry.hpp"

using namespace aegis::edge;

TEST(RateLimiter, SetStateAllowsBurst)
{
    RateLimiter rl;
    // Initial tokens = burst = 10; should allow 10 back-to-back at t=0.
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_TRUE(rl.Allow(CmdId::kSetState, 0U))
            << "Expected Allow at i=" << i;
    }
}

TEST(RateLimiter, SetStateRejectsAfterBurst)
{
    RateLimiter rl;
    for (int i = 0; i < 10; ++i) { (void)rl.Allow(CmdId::kSetState, 0U); }
    EXPECT_FALSE(rl.Allow(CmdId::kSetState, 0U));
}

TEST(RateLimiter, SetStateRefillsAfterDelay)
{
    RateLimiter rl;
    for (int i = 0; i < 10; ++i) { (void)rl.Allow(CmdId::kSetState, 0U); }
    // After 1 s (5 tokens refilled at 5 req/s), allow 5 more.
    EXPECT_TRUE(rl.Allow(CmdId::kSetState, 1000U));
    EXPECT_TRUE(rl.Allow(CmdId::kSetState, 1000U));
    EXPECT_TRUE(rl.Allow(CmdId::kSetState, 1000U));
    EXPECT_TRUE(rl.Allow(CmdId::kSetState, 1000U));
    EXPECT_TRUE(rl.Allow(CmdId::kSetState, 1000U));
    EXPECT_FALSE(rl.Allow(CmdId::kSetState, 1000U)); // exhausted again
}

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
