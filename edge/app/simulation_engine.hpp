#ifndef AEGIS_EDGE_SIMULATION_ENGINE_HPP
#define AEGIS_EDGE_SIMULATION_ENGINE_HPP

#include <optional>

#include "domain.hpp"

namespace aegis::edge {

class SimulationEngine
{
public:
    explicit SimulationEngine(std::uint32_t seed);

    [[nodiscard]] std::optional<Event> Tick100ms(SystemState state,
                                                 std::uint32_t timestamp_ms);

private:
    [[nodiscard]] bool Roll(std::uint32_t denominator);
    [[nodiscard]] std::uint32_t NextRandom();

    std::uint32_t prng_state_;
};

} // namespace aegis::edge

#endif
