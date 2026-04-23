#include <gtest/gtest.h>
#include "replay_guard.hpp"

using namespace aegis::edge;

TEST(ReplayGuard, AcceptsFirstFrame)
{
    ReplayGuard rg;
    EXPECT_TRUE(rg.Check(1U));
}

TEST(ReplayGuard, AcceptsStrictlyIncreasing)
{
    ReplayGuard rg;
    EXPECT_TRUE(rg.Check(1U));
    EXPECT_TRUE(rg.Check(2U));
    EXPECT_TRUE(rg.Check(100U));
}

TEST(ReplayGuard, RejectsReplay)
{
    ReplayGuard rg;
    EXPECT_TRUE(rg.Check(5U));
    EXPECT_FALSE(rg.Check(5U));  // same seq
    EXPECT_FALSE(rg.Check(4U));  // older
    EXPECT_FALSE(rg.Check(1U));
}

TEST(ReplayGuard, RejectsZeroAfterFirstAccept)
{
    ReplayGuard rg;
    EXPECT_TRUE(rg.Check(1U));
    EXPECT_FALSE(rg.Check(0U));
}

TEST(ReplayGuard, AcceptsSeqZeroAsFirst)
{
    ReplayGuard rg;
    EXPECT_TRUE(rg.Check(0U));  // first frame seeds the counter
    EXPECT_FALSE(rg.Check(0U)); // replay rejected
    EXPECT_TRUE(rg.Check(1U));  // strictly greater accepted
}

TEST(ReplayGuard, ResetRestoresFreshState)
{
    ReplayGuard rg;
    EXPECT_TRUE(rg.Check(42U));
    rg.Reset();
    EXPECT_TRUE(rg.Check(1U));   // fresh after reset
}
