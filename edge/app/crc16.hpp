#ifndef AEGIS_EDGE_CRC16_HPP
#define AEGIS_EDGE_CRC16_HPP

#include <cstdint>

namespace aegis::edge {

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no input/output reflection,
// XOR-out 0x0000. Used by the AC2 frame format (IRS §3.1).
[[nodiscard]] std::uint16_t CRC16(const std::uint8_t* data,
                                   std::uint8_t len) noexcept;

} // namespace aegis::edge

#endif
