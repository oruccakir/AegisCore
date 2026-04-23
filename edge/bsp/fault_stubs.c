/*
 * Naked fault-handler trampolines.
 * Each handler captures the faulting stack pointer in r0 and tail-calls
 * Panic_HardFaultImpl (defined in bsp/panic.cpp, extern "C").
 *
 * Cortex-M4 exception frame layout at [sp]:
 *   r0, r1, r2, r3, r12, lr, pc, xpsr
 */

#include <stdint.h>

extern void Panic_HardFaultImpl(uint32_t* fault_frame);

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4         \n\t"
        "ite eq             \n\t"
        "mrseq r0, msp      \n\t"
        "mrsne r0, psp      \n\t"
        "b Panic_HardFaultImpl \n\t"
    );
}

__attribute__((naked)) void MemManage_Handler(void)
{
    __asm volatile(
        "tst lr, #4         \n\t"
        "ite eq             \n\t"
        "mrseq r0, msp      \n\t"
        "mrsne r0, psp      \n\t"
        "b Panic_HardFaultImpl \n\t"
    );
}

__attribute__((naked)) void BusFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4         \n\t"
        "ite eq             \n\t"
        "mrseq r0, msp      \n\t"
        "mrsne r0, psp      \n\t"
        "b Panic_HardFaultImpl \n\t"
    );
}

__attribute__((naked)) void UsageFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4         \n\t"
        "ite eq             \n\t"
        "mrseq r0, msp      \n\t"
        "mrsne r0, psp      \n\t"
        "b Panic_HardFaultImpl \n\t"
    );
}
