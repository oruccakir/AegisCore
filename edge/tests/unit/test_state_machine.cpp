#include <gtest/gtest.h>
#include "state_machine.hpp"
#include "domain.hpp"

using namespace aegis::edge;

static Event MakeEvent(EventType type, std::uint32_t ts = 0U)
{
    return Event{type, ts};
}

TEST(StateMachine, InitialStateIsIdle)
{
    StateMachine sm;
    EXPECT_EQ(sm.state(), SystemState::Idle);
}

TEST(StateMachine, IdleToSearchOnShortPress)
{
    StateMachine sm;
    EXPECT_TRUE(sm.Dispatch(MakeEvent(EventType::ButtonShortPress)));
    EXPECT_EQ(sm.state(), SystemState::Search);
}

TEST(StateMachine, SearchToIdleOnLongPress)
{
    StateMachine sm;
    (void)sm.Dispatch(MakeEvent(EventType::ButtonShortPress));
    EXPECT_TRUE(sm.Dispatch(MakeEvent(EventType::ButtonLongPress)));
    EXPECT_EQ(sm.state(), SystemState::Idle);
}

TEST(StateMachine, SearchToTrackOnTargetDetected)
{
    StateMachine sm;
    (void)sm.Dispatch(MakeEvent(EventType::ButtonShortPress));
    EXPECT_TRUE(sm.Dispatch(MakeEvent(EventType::SimTargetDetected)));
    EXPECT_EQ(sm.state(), SystemState::Track);
}

TEST(StateMachine, TrackToSearchOnTargetLost)
{
    StateMachine sm;
    (void)sm.Dispatch(MakeEvent(EventType::ButtonShortPress));
    (void)sm.Dispatch(MakeEvent(EventType::SimTargetDetected));
    EXPECT_TRUE(sm.Dispatch(MakeEvent(EventType::SimTargetLost)));
    EXPECT_EQ(sm.state(), SystemState::Search);
}

TEST(StateMachine, TrackToIdleOnLongPress)
{
    StateMachine sm;
    (void)sm.Dispatch(MakeEvent(EventType::ButtonShortPress));
    (void)sm.Dispatch(MakeEvent(EventType::SimTargetDetected));
    EXPECT_TRUE(sm.Dispatch(MakeEvent(EventType::ButtonLongPress)));
    EXPECT_EQ(sm.state(), SystemState::Idle);
}

TEST(StateMachine, FailSafeIsTerminal)
{
    StateMachine sm;
    sm.ForceFailSafe(0U);
    EXPECT_EQ(sm.state(), SystemState::FailSafe);
    // All events are rejected in FailSafe.
    EXPECT_FALSE(sm.Dispatch(MakeEvent(EventType::ButtonShortPress)));
    EXPECT_FALSE(sm.Dispatch(MakeEvent(EventType::ButtonLongPress)));
    EXPECT_EQ(sm.state(), SystemState::FailSafe);
}

TEST(StateMachine, PrevStateTracked)
{
    StateMachine sm;
    EXPECT_EQ(sm.prev_state(), SystemState::Idle);
    (void)sm.Dispatch(MakeEvent(EventType::ButtonShortPress));
    EXPECT_EQ(sm.prev_state(), SystemState::Idle);
    EXPECT_EQ(sm.state(), SystemState::Search);
}

TEST(StateMachine, ForceState_IdleNotTerminal)
{
    StateMachine sm;
    (void)sm.Dispatch(MakeEvent(EventType::ButtonShortPress));
    sm.ForceState(SystemState::Idle, 0U);
    EXPECT_EQ(sm.state(), SystemState::Idle);
    // Should accept new transitions from Idle.
    EXPECT_TRUE(sm.Dispatch(MakeEvent(EventType::ButtonShortPress)));
}

TEST(StateMachine, IdleRejectsNonShortPress)
{
    StateMachine sm;
    EXPECT_FALSE(sm.Dispatch(MakeEvent(EventType::ButtonLongPress)));
    EXPECT_FALSE(sm.Dispatch(MakeEvent(EventType::SimTargetDetected)));
    EXPECT_EQ(sm.state(), SystemState::Idle);
}
