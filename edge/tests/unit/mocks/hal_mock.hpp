#ifndef AEGIS_EDGE_HAL_MOCK_HPP
#define AEGIS_EDGE_HAL_MOCK_HPP

// Minimal stubs so headers that transitively reference HAL types
// can compile on the host.  Only add what unit tests actually need.

#include <cstdint>

// Prevent re-inclusion of real HAL by defining the guard macros.
#define STM32F4xx_H
#define __STM32F4xx_HAL_H

inline std::uint32_t HAL_GetTick() { return 0U; }

#endif
