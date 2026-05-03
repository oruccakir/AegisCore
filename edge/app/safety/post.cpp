#include "post.hpp"

#include "platform_io.hpp"

namespace {

bool RamTest() noexcept
{
    volatile std::uint8_t scratch[1024U];
    for (auto& b : scratch) { b = 0x55U; }
    for (const auto& b : scratch) { if (b != 0x55U) { return false; } }
    for (auto& b : scratch) { b = 0xAAU; }
    for (const auto& b : scratch) { if (b != 0xAAU) { return false; } }
    return true;
}

void LedSelfTest() noexcept
{
    aegis::edge::ApplyLedOutputs({true, false});
    aegis::edge::DelayMs(200U);
    aegis::edge::ApplyLedOutputs({false, true});
    aegis::edge::DelayMs(200U);
    aegis::edge::ApplyLedOutputs({false, false});
}

// Flash CRC stub — skipped until CI injects __expected_flash_crc symbol.
bool FlashCrcTest() noexcept
{
    return true;
}

} // namespace

namespace aegis::edge {

bool POST_Run() noexcept
{
    if (!FlashCrcTest()) { return false; }
    if (!RamTest())      { return false; }
    if (!IsHseClockReady()) { return false; }
    LedSelfTest();
    return true;
}

} // namespace aegis::edge
