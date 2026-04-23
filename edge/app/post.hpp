#ifndef AEGIS_EDGE_POST_HPP
#define AEGIS_EDGE_POST_HPP

namespace aegis::edge {

// Power-On Self-Test / Built-In-Test (SAF-07).
// Returns true if all tests pass. On failure the caller should spin;
// the watchdog will reset the device.
// Must be called after InitializePlatform() and before vTaskStartScheduler().
[[nodiscard]] bool POST_Run() noexcept;

} // namespace aegis::edge

#endif
