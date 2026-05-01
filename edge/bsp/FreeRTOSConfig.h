#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

extern uint32_t SystemCoreClock;

#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCPU_CLOCK_HZ                      ( SystemCoreClock )
#define configTICK_RATE_HZ                      1000U
#define configMAX_PRIORITIES                    8
#define configMINIMAL_STACK_SIZE                ( ( uint16_t ) 128 )
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       0
#define configQUEUE_REGISTRY_SIZE               0
#define configCHECK_FOR_STACK_OVERFLOW          2   /* SAF-05 */
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_APPLICATION_TASK_TAG          0
#define configUSE_COUNTING_SEMAPHORES           0
#define configUSE_TRACE_FACILITY                1   /* for uxTaskGetSystemState */
#define configGENERATE_RUN_TIME_STATS           1
/* DWT cycle counter as run-time stats clock (1 tick = 1 cycle @ 168 MHz).
   Raw addresses used so this header compiles in C translation units that
   do not include the CMSIS core_cm4.h device header (e.g. FreeRTOS tasks.c). */
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() \
    do { \
        *((volatile uint32_t*)0xE000EDFC) |= (1UL << 24); \
        *((volatile uint32_t*)0xE0001004)  = 0U; \
        *((volatile uint32_t*)0xE0001000) |= 1UL; \
    } while(0)
#define portGET_RUN_TIME_COUNTER_VALUE() \
    (*((volatile uint32_t*)0xE0001004) / 168U)
#define configUSE_TICKLESS_IDLE                 0
#define configUSE_TIMERS                        0
#define configSUPPORT_DYNAMIC_ALLOCATION        0
#define configSUPPORT_STATIC_ALLOCATION         1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1

#define configPRIO_BITS                         4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

#ifdef __cplusplus
extern "C" {
#endif
void vAssertCalled(const char* file, int line);
#ifdef __cplusplus
}
#endif

#define configASSERT(x) \
    do \
    { \
        if ((x) == 0) \
        { \
            vAssertCalled(__FILE__, __LINE__); \
        } \
    } while (0)

#define INCLUDE_vTaskPrioritySet                0
#define INCLUDE_uxTaskPriorityGet               0
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    0
#define INCLUDE_xResumeFromISR                  0
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_uxTaskGetStackHighWaterMark     1

#define vPortSVCHandler                         SVC_Handler
#define xPortPendSVHandler                      PendSV_Handler

#endif
