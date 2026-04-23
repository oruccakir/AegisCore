#ifndef AEGIS_EDGE_PANIC_HPP
#define AEGIS_EDGE_PANIC_HPP

#include <cstdint>

namespace aegis::edge {

// Preserved across watchdog/hard-fault resets in .noinit section.
struct PanicBlock
{
    std::uint32_t magic;    // 0xDEADBEEF when populated
    std::uint32_t r0;
    std::uint32_t r1;
    std::uint32_t r2;
    std::uint32_t r3;
    std::uint32_t r12;
    std::uint32_t lr;       // link register at fault
    std::uint32_t pc;       // program counter at fault
    std::uint32_t xpsr;
    std::uint32_t cfsr;     // Configurable Fault Status
    std::uint32_t hfsr;     // HardFault Status
    std::uint32_t mmfar;    // MemManage Fault Address
    std::uint32_t bfar;     // BusFault Address
    std::uint32_t reset_reason; // RCC_CSR bits captured after boot
    char task_name[16];     // task name from stack overflow hook
};

static_assert(sizeof(PanicBlock) <= 72U, "PanicBlock exceeds budget");

inline constexpr std::uint32_t kPanicMagic = 0xDEADBEEFU;

PanicBlock* GetPanicBlock() noexcept;

} // namespace aegis::edge

// Called from the naked HardFault/BusFault/UsageFault/MemManage assembly stubs.
extern "C" void Panic_HardFaultImpl(std::uint32_t* fault_frame) noexcept;

#endif
