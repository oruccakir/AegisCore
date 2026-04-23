#include <gtest/gtest.h>
#include "button_classifier.hpp"
#include "domain.hpp"

using namespace aegis::edge;

static RawButtonEdge Press(std::uint32_t ts)
{
    return RawButtonEdge{RawButtonEdgeType::Pressed, ts};
}

static RawButtonEdge Release(std::uint32_t ts)
{
    return RawButtonEdge{RawButtonEdgeType::Released, ts};
}

TEST(ButtonClassifier, ShortPress)
{
    ButtonClassifier bc;
    // Press at t=0, release at t=100ms (< 500ms threshold).
    EXPECT_FALSE(bc.OnEdge(Press(0U)).has_value());
    const auto ev = bc.OnEdge(Release(100U));
    ASSERT_TRUE(ev.has_value());
    EXPECT_EQ(ev->type, EventType::ButtonShortPress);
}

TEST(ButtonClassifier, LongPress)
{
    ButtonClassifier bc;
    EXPECT_FALSE(bc.OnEdge(Press(0U)).has_value());
    const auto ev = bc.OnEdge(Release(600U));  // > 500ms
    ASSERT_TRUE(ev.has_value());
    EXPECT_EQ(ev->type, EventType::ButtonLongPress);
}

TEST(ButtonClassifier, BouncedPressIgnored)
{
    ButtonClassifier bc;
    // First press + release within debounce window (< 20ms).
    EXPECT_FALSE(bc.OnEdge(Press(0U)).has_value());
    const auto ev = bc.OnEdge(Release(10U));  // < 20ms debounce
    EXPECT_FALSE(ev.has_value());
}

TEST(ButtonClassifier, TwoConsecutiveShortPresses)
{
    ButtonClassifier bc;
    EXPECT_FALSE(bc.OnEdge(Press(0U)).has_value());
    auto ev = bc.OnEdge(Release(100U));
    ASSERT_TRUE(ev.has_value());
    EXPECT_EQ(ev->type, EventType::ButtonShortPress);

    EXPECT_FALSE(bc.OnEdge(Press(200U)).has_value());
    ev = bc.OnEdge(Release(300U));
    ASSERT_TRUE(ev.has_value());
    EXPECT_EQ(ev->type, EventType::ButtonShortPress);
}

TEST(ButtonClassifier, ExactThresholdIsLong)
{
    ButtonClassifier bc;
    EXPECT_FALSE(bc.OnEdge(Press(0U)).has_value());
    const auto ev = bc.OnEdge(Release(500U));  // exactly 500ms
    ASSERT_TRUE(ev.has_value());
    EXPECT_EQ(ev->type, EventType::ButtonLongPress);
}
