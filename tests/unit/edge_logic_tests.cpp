#include <cstdlib>
#include <iostream>

#include "button_classifier.hpp"
#include "simulation_engine.hpp"
#include "state_machine.hpp"

namespace {

int g_failures = 0;

void ExpectTrue(const bool condition, const char* expression,
                const char* function_name, const int line)
{
    if (!condition)
    {
        std::cerr << function_name << ':' << line
                  << " expectation failed: " << expression << '\n';
        ++g_failures;
    }
}

#define EXPECT_TRUE(expr) ExpectTrue((expr), #expr, __func__, __LINE__)

using aegis::edge::ButtonClassifier;
using aegis::edge::EventType;
using aegis::edge::RawButtonEdge;
using aegis::edge::RawButtonEdgeType;
using aegis::edge::SimulationEngine;
using aegis::edge::StateMachine;
using aegis::edge::SystemState;

void TestStateTransitions()
{
    StateMachine state_machine{0U};

    EXPECT_TRUE(state_machine.state() == SystemState::Idle);
    EXPECT_TRUE(state_machine.Dispatch({EventType::ButtonShortPress, 10U}));
    EXPECT_TRUE(state_machine.state() == SystemState::Search);
    EXPECT_TRUE(state_machine.Dispatch({EventType::SimTargetDetected, 110U}));
    EXPECT_TRUE(state_machine.state() == SystemState::Track);
    EXPECT_TRUE(state_machine.Dispatch({EventType::SimTargetLost, 210U}));
    EXPECT_TRUE(state_machine.state() == SystemState::Search);
    EXPECT_TRUE(state_machine.Dispatch({EventType::ButtonLongPress, 710U}));
    EXPECT_TRUE(state_machine.state() == SystemState::Idle);
}

void TestInvalidTransitions()
{
    StateMachine state_machine{0U};

    EXPECT_TRUE(!state_machine.Dispatch({EventType::ButtonLongPress, 50U}));
    EXPECT_TRUE(!state_machine.Dispatch({EventType::SimTargetDetected, 100U}));
    EXPECT_TRUE(state_machine.Dispatch({EventType::ButtonShortPress, 150U}));
    EXPECT_TRUE(!state_machine.Dispatch({EventType::ButtonShortPress, 200U}));
    EXPECT_TRUE(!state_machine.Dispatch({EventType::SimTargetLost, 250U}));
    EXPECT_TRUE(state_machine.Dispatch({EventType::SimTargetDetected, 300U}));
    EXPECT_TRUE(!state_machine.Dispatch({EventType::ButtonShortPress, 350U}));
}

void TestLedOutputs()
{
    StateMachine state_machine{0U};

    const auto idle = state_machine.GetLedOutputs(0U);
    EXPECT_TRUE(!idle.green_on);
    EXPECT_TRUE(!idle.red_on);

    EXPECT_TRUE(state_machine.Dispatch({EventType::ButtonShortPress, 100U}));
    const auto search_start = state_machine.GetLedOutputs(100U);
    const auto search_mid = state_machine.GetLedOutputs(599U);
    const auto search_toggle = state_machine.GetLedOutputs(600U);

    EXPECT_TRUE(search_start.green_on);
    EXPECT_TRUE(!search_start.red_on);
    EXPECT_TRUE(search_mid.green_on);
    EXPECT_TRUE(!search_toggle.green_on);

    EXPECT_TRUE(state_machine.Dispatch({EventType::SimTargetDetected, 900U}));
    const auto track = state_machine.GetLedOutputs(900U);
    EXPECT_TRUE(!track.green_on);
    EXPECT_TRUE(track.red_on);
}

void TestButtonClassifier()
{
    ButtonClassifier classifier;

    EXPECT_TRUE(!classifier.OnEdge({RawButtonEdgeType::Released, 0U}).has_value());
    EXPECT_TRUE(!classifier.OnEdge({RawButtonEdgeType::Pressed, 100U}).has_value());
    EXPECT_TRUE(!classifier.OnEdge({RawButtonEdgeType::Released, 105U}).has_value());

    const auto short_press = classifier.OnEdge({RawButtonEdgeType::Released, 180U});
    EXPECT_TRUE(short_press.has_value());
    EXPECT_TRUE(short_press->type == EventType::ButtonShortPress);

    EXPECT_TRUE(!classifier.OnEdge({RawButtonEdgeType::Pressed, 500U}).has_value());
    EXPECT_TRUE(!classifier.OnEdge({RawButtonEdgeType::Pressed, 505U}).has_value());

    const auto long_press = classifier.OnEdge({RawButtonEdgeType::Released, 1100U});
    EXPECT_TRUE(long_press.has_value());
    EXPECT_TRUE(long_press->type == EventType::ButtonLongPress);
}

void TestSimulationDeterminism()
{
    constexpr std::uint32_t kKnownSeed = 0x12345678U;

    SimulationEngine lhs{kKnownSeed};
    SimulationEngine rhs{kKnownSeed};
    bool saw_detect = false;

    for (std::uint32_t tick = 1U; tick <= 400U; ++tick)
    {
        const auto timestamp_ms = tick * 100U;
        const auto left_event = lhs.Tick100ms(SystemState::Search, timestamp_ms);
        const auto right_event = rhs.Tick100ms(SystemState::Search, timestamp_ms);

        EXPECT_TRUE(left_event.has_value() == right_event.has_value());
        if (left_event.has_value())
        {
            saw_detect = true;
            EXPECT_TRUE(right_event.has_value());
            EXPECT_TRUE(left_event->type == EventType::SimTargetDetected);
            EXPECT_TRUE(left_event->type == right_event->type);
            EXPECT_TRUE(left_event->timestamp_ms == right_event->timestamp_ms);
        }
    }

    EXPECT_TRUE(saw_detect);

    SimulationEngine idle_engine{kKnownSeed};
    for (std::uint32_t tick = 1U; tick <= 32U; ++tick)
    {
        EXPECT_TRUE(!idle_engine.Tick100ms(SystemState::Idle, tick * 100U).has_value());
    }
}

} // namespace

int main()
{
    TestStateTransitions();
    TestInvalidTransitions();
    TestLedOutputs();
    TestButtonClassifier();
    TestSimulationDeterminism();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " test expectation(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "edge-unit-tests passed\n";
    return EXIT_SUCCESS;
}
