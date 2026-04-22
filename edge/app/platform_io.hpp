#ifndef AEGIS_EDGE_PLATFORM_IO_HPP
#define AEGIS_EDGE_PLATFORM_IO_HPP

#include <cstdint>

#include "domain.hpp"

namespace aegis::edge {

void ApplyLedOutputs(const LedOutputs& outputs);
void InitializePlatform();
[[nodiscard]] std::uint32_t MillisecondsSinceBoot();
[[nodiscard]] bool ReadButtonPressed();

} // namespace aegis::edge

extern "C" void Aegis_HandleExti0Irq(void);

#endif
