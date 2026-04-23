#ifndef AEGIS_EDGE_WATCHDOG_HPP
#define AEGIS_EDGE_WATCHDOG_HPP

namespace aegis::edge {

// Thin wrapper around STM32 IWDG (Independent Watchdog).
// LSI ~32 kHz, prescaler /32, reload 1023 → timeout ~1024 ms.
// Init() must be called once at startup before the scheduler.
// Only StateMachineTask should call Feed(); starvation triggers reset.
class Watchdog
{
public:
    static void Init() noexcept;
    static void Feed() noexcept;
};

} // namespace aegis::edge

#endif
