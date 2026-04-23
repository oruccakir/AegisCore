#ifndef AEGIS_EDGE_STATE_MACHINE_HPP
#define AEGIS_EDGE_STATE_MACHINE_HPP

#include "domain.hpp"

namespace aegis::edge {

class StateMachine
{
public:
    explicit StateMachine(std::uint32_t initial_timestamp_ms = 0U);

    [[nodiscard]] bool Dispatch(const Event& event);
    [[nodiscard]] LedOutputs GetLedOutputs(std::uint32_t timestamp_ms) const;

    // Force transitions (gateway commands / fail-safe supervisor).
    void ForceFailSafe(std::uint32_t timestamp_ms) noexcept;
    void ForceState(SystemState next, std::uint32_t timestamp_ms) noexcept;

    [[nodiscard]] SystemState state()      const noexcept { return state_; }
    [[nodiscard]] SystemState prev_state() const noexcept { return prev_state_; }

private:
    static constexpr std::uint32_t kSearchBlinkToggleMs = 500U;

    void TransitionTo(SystemState next_state, std::uint32_t timestamp_ms) noexcept;

    SystemState   state_             = SystemState::Idle;
    SystemState   prev_state_        = SystemState::Idle;
    std::uint32_t search_started_ms_ = 0U;
};

} // namespace aegis::edge

#endif
