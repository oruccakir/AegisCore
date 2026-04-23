#include "state_machine.hpp"

namespace aegis::edge {

StateMachine::StateMachine(const std::uint32_t initial_timestamp_ms)
    : search_started_ms_(initial_timestamp_ms)
{
}

bool StateMachine::Dispatch(const Event& event)
{
    switch (state_)
    {
    case SystemState::Idle:
        if (event.type == EventType::ButtonShortPress)
        {
            TransitionTo(SystemState::Search, event.timestamp_ms);
            return true;
        }
        return false;

    case SystemState::Search:
        if (event.type == EventType::ButtonLongPress)
        {
            TransitionTo(SystemState::Idle, event.timestamp_ms);
            return true;
        }

        if (event.type == EventType::SimTargetDetected)
        {
            TransitionTo(SystemState::Track, event.timestamp_ms);
            return true;
        }
        return false;

    case SystemState::Track:
        if (event.type == EventType::ButtonLongPress)
        {
            TransitionTo(SystemState::Idle, event.timestamp_ms);
            return true;
        }

        if (event.type == EventType::SimTargetLost)
        {
            TransitionTo(SystemState::Search, event.timestamp_ms);
            return true;
        }
        return false;

    case SystemState::FailSafe:
        return false;
    }

    return false;
}

LedOutputs StateMachine::GetLedOutputs(const std::uint32_t timestamp_ms) const
{
    if (state_ == SystemState::Search)
    {
        const std::uint32_t elapsed_ms = timestamp_ms - search_started_ms_;
        const bool green_on = ((elapsed_ms / kSearchBlinkToggleMs) % 2U) == 0U;
        return LedOutputs{green_on, false};
    }

    if (state_ == SystemState::Track)
    {
        return LedOutputs{false, true};
    }

    return LedOutputs{false, false};
}

void StateMachine::TransitionTo(const SystemState next_state,
                                const std::uint32_t timestamp_ms) noexcept
{
    prev_state_ = state_;
    state_      = next_state;

    if (next_state == SystemState::Search)
    {
        search_started_ms_ = timestamp_ms;
    }
}

void StateMachine::ForceFailSafe(const std::uint32_t timestamp_ms) noexcept
{
    TransitionTo(SystemState::FailSafe, timestamp_ms);
}

void StateMachine::ForceState(const SystemState next,
                               const std::uint32_t timestamp_ms) noexcept
{
    TransitionTo(next, timestamp_ms);
}

} // namespace aegis::edge
