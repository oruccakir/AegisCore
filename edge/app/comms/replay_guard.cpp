#include "replay_guard.hpp"

namespace aegis::edge {

bool ReplayGuard::Check(std::uint32_t seq) noexcept
{
    if (!initialized_)
    {
        // Accept the first frame unconditionally to seed the counter.
        last_seq_    = seq;
        initialized_ = true;
        return true;
    }

    if (seq <= last_seq_)
    {
        return false;
    }

    last_seq_ = seq;
    return true;
}

} // namespace aegis::edge
