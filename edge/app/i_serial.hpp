#ifndef AEGIS_EDGE_I_SERIAL_HPP
#define AEGIS_EDGE_I_SERIAL_HPP

#include <cstdint>

namespace aegis::edge {

// Abstract serial byte-stream interface. Implemented by UartDriver in BSP.
// All lengths are in bytes; max frame payload is 66 bytes (AC2 max frame).
class ISerial
{
public:
    virtual ~ISerial() = default;

    // Blocking write. Returns true on success.
    virtual bool Write(const std::uint8_t* data, std::uint8_t len) noexcept = 0;

    // Non-blocking read. Returns bytes actually copied (0 if nothing ready).
    virtual std::uint8_t Read(std::uint8_t* dst, std::uint8_t max_len) noexcept = 0;
};

} // namespace aegis::edge

#endif
