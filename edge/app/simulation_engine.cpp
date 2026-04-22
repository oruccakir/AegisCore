#include "simulation_engine.hpp"

namespace aegis::edge {

namespace {

constexpr std::uint32_t kFallbackSeed = 0x1A2B3C4DU;
constexpr std::uint32_t kSearchDetectDenominator = 50U; /* p = 0.02 */
constexpr std::uint32_t kTrackLossDenominator = 200U;   /* p = 0.005 */

} // namespace

SimulationEngine::SimulationEngine(const std::uint32_t seed)
    : prng_state_(seed == 0U ? kFallbackSeed : seed)
{
}

std::optional<Event> SimulationEngine::Tick100ms(const SystemState state,
                                                 const std::uint32_t timestamp_ms)
{
    if (state == SystemState::Search)
    {
        if (Roll(kSearchDetectDenominator))
        {
            return Event{EventType::SimTargetDetected, timestamp_ms};
        }

        return std::nullopt;
    }

    if (state == SystemState::Track)
    {
        if (Roll(kTrackLossDenominator))
        {
            return Event{EventType::SimTargetLost, timestamp_ms};
        }

        return std::nullopt;
    }

    return std::nullopt;
}

bool SimulationEngine::Roll(const std::uint32_t denominator)
{
    return (NextRandom() % denominator) == 0U;
}

std::uint32_t SimulationEngine::NextRandom()
{
    std::uint32_t value = prng_state_;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    prng_state_ = value;
    return value;
}

} // namespace aegis::edge
