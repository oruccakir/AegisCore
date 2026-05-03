#ifndef AEGIS_EDGE_REPLAY_GUARD_HPP
#define AEGIS_EDGE_REPLAY_GUARD_HPP

#include <cstdint>

namespace aegis::edge {

// Monotonic sequence-number replay protection (SR-02).
// Rejects frames whose SEQ_NUM <= the last accepted value.
// State resets on construction (power-on / reboot).
class ReplayGuard
{
public:
    // Returns true if seq is strictly greater than the last accepted value.
    // Advances the internal counter on accept.
    [[nodiscard]] bool Check(std::uint32_t seq) noexcept;

    void Reset() noexcept { last_seq_ = 0U; initialized_ = false; }

private:
    std::uint32_t last_seq_    = 0U;
    bool          initialized_ = false;
};

} // namespace aegis::edge

#endif
