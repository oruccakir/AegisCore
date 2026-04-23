#include "panic.hpp"

#include "stm32f4xx_hal.h"

// Panic block lives in .noinit — not zeroed by startup, survives watchdog resets.
__attribute__((section(".noinit")))
static aegis::edge::PanicBlock g_panic_block;

namespace aegis::edge {

PanicBlock* GetPanicBlock() noexcept
{
    return &g_panic_block;
}

} // namespace aegis::edge

// Called from naked fault handler stubs in stm32f4xx_it.c.
// fault_frame points to the exception stack frame: r0,r1,r2,r3,r12,lr,pc,xpsr.
extern "C" void Panic_HardFaultImpl(std::uint32_t* fault_frame) noexcept
{
    g_panic_block.magic = aegis::edge::kPanicMagic;
    g_panic_block.r0    = fault_frame[0U];
    g_panic_block.r1    = fault_frame[1U];
    g_panic_block.r2    = fault_frame[2U];
    g_panic_block.r3    = fault_frame[3U];
    g_panic_block.r12   = fault_frame[4U];
    g_panic_block.lr    = fault_frame[5U];
    g_panic_block.pc    = fault_frame[6U];
    g_panic_block.xpsr  = fault_frame[7U];

    g_panic_block.cfsr  = SCB->CFSR;
    g_panic_block.hfsr  = SCB->HFSR;
    g_panic_block.mmfar = SCB->MMFAR;
    g_panic_block.bfar  = SCB->BFAR;

    // Capture reset cause from RCC_CSR (read before HAL clears it).
    g_panic_block.reset_reason = RCC->CSR;

    NVIC_SystemReset();
}
