#ifndef AEGIS_EDGE_LCD_DRIVER_HPP
#define AEGIS_EDGE_LCD_DRIVER_HPP

#include <cstdint>

namespace aegis::edge {

[[nodiscard]] bool InitializeLcd() noexcept;
void LcdClear() noexcept;
void LcdWriteLines(const char* line0, const char* line1) noexcept;

} // namespace aegis::edge

#endif
