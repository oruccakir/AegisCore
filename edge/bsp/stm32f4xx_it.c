/*
 * Interrupt Service Routine handlers for STM32F407G-DISC1.
 *
 * FreeRTOS owns SVC and PendSV through handler aliases declared in
 * FreeRTOSConfig.h. SysTick remains application-owned so we can advance the HAL
 * timebase and then hand control to the kernel once the scheduler is running.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx_hal.h"

extern void Aegis_HandleExti0Irq(void);
extern void xPortSysTickHandler(void);

// Fault handlers are in bsp/fault_stubs.c (naked trampolines → Panic_HardFaultImpl).

extern void UartDriver_OnUsart2Irq(void);
extern void UartDriver_OnDma1Stream5Irq(void);

void NMI_Handler(void) { while (1) { } }
void DebugMon_Handler(void) { }

void EXTI0_IRQHandler(void)
{
    Aegis_HandleExti0Irq();
}

void USART2_IRQHandler(void)
{
    UartDriver_OnUsart2Irq();
}

void DMA1_Stream5_IRQHandler(void)
{
    UartDriver_OnDma1Stream5Irq();
}

void SysTick_Handler(void)
{
    HAL_IncTick();

    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        xPortSysTickHandler();
    }
}
