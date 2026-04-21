/*
 * Interrupt Service Routine handlers for STM32F407G-DISC1.
 *
 * Startup (startup_stm32f407xx.s) declares all ISR symbols as .weak aliases of
 * Default_Handler (infinite loop). Defining a strong symbol here overrides it.
 *
 * For Phase 1 we only need SysTick (HAL timebase). Fault handlers are trapped
 * as infinite loops — SAF-06 (post-mortem register dump) comes in Phase 7.
 */

#include "stm32f4xx_hal.h"

/* ------------------------------------------------------------------------- */
/* Cortex-M4 Processor Exception Handlers                                    */
/* ------------------------------------------------------------------------- */

void NMI_Handler(void)        { while (1) { } }
void HardFault_Handler(void)  { while (1) { } }
void MemManage_Handler(void)  { while (1) { } }
void BusFault_Handler(void)   { while (1) { } }
void UsageFault_Handler(void) { while (1) { } }
void SVC_Handler(void)        { /* RTOS uses this later. */ }
void DebugMon_Handler(void)   { }
void PendSV_Handler(void)     { /* RTOS context switch later. */ }

/**
 * @brief 1-kHz tick used by HAL_Delay() / HAL_GetTick().
 * Configured by HAL_InitTick() via SysTick_Config() at core frequency / 1000.
 */
void SysTick_Handler(void)
{
    HAL_IncTick();
}
