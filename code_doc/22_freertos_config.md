# FreeRTOSConfig.h — Kernel Configuration

**File:** `edge/bsp/FreeRTOSConfig.h`

---

## What this file is

`FreeRTOSConfig.h` is the configuration file for the FreeRTOS kernel. Every `#define` here controls a kernel feature or parameter. This file is included by FreeRTOS's own source files, so every change directly affects the compiled kernel.

---

## Scheduler settings

```c
#define configUSE_PREEMPTION    1
```
Enable preemptive scheduling. A higher-priority task can interrupt a lower-priority task at any time (at the next SysTick tick or when the higher-priority task becomes ready). The alternative (cooperative) requires tasks to explicitly yield — not suitable for a real-time system.

```c
#define configCPU_CLOCK_HZ      ( SystemCoreClock )
```
Tells FreeRTOS the CPU frequency. `SystemCoreClock` is a CMSIS variable set to 168000000 by `SystemClock_Config()`. FreeRTOS uses this to configure SysTick.

```c
#define configTICK_RATE_HZ      1000U
```
FreeRTOS tick rate: 1000 ticks per second = 1 tick every 1 ms. This means `pdMS_TO_TICKS(100)` = 100 ticks. The minimum delay resolution is 1 ms.

```c
#define configMAX_PRIORITIES    8
```
Task priorities 0–7 are valid. Priority 0 is the Idle task. AegisCore uses priorities 2–5.

---

## Memory settings

```c
#define configSUPPORT_DYNAMIC_ALLOCATION   0
#define configSUPPORT_STATIC_ALLOCATION    1
```

**This is the most important pair of settings.** Dynamic allocation is disabled entirely — `pvPortMalloc` cannot be called. All tasks, queues, and semaphores must be created using the Static variants. This enforces the project's no-heap rule.

```c
#define configMINIMAL_STACK_SIZE    ( ( uint16_t ) 128 )
```
Minimum stack size in words (= 512 bytes). Used for the Idle task.

---

## Stack overflow detection

```c
#define configCHECK_FOR_STACK_OVERFLOW    2    /* SAF-05 */
```

Mode 2 (the most thorough): at each context switch, FreeRTOS checks:
1. The last few bytes of the task's stack for a known fill pattern (tasks stacks are filled with `0xA5` at creation)
2. That the stack pointer hasn't gone below the stack bottom

If either check fails, `vApplicationStackOverflowHook()` is called. The `/* SAF-05 */` comment traces this setting to requirement SAF-05.

---

## Interrupt priorities

```c
#define configPRIO_BITS                          4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY  15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY  5
```

The STM32F407 has 4 priority bits → 16 priority levels (0 = highest, 15 = lowest).

```c
#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
    // = 15 << 4 = 240
```
The kernel's own interrupts (SysTick, PendSV, SVC) run at the lowest hardware priority (240 in the 8-bit NVIC register format).

```c
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
    // = 5 << 4 = 80
```

Any ISR that calls FreeRTOS `FromISR` functions must have a numeric priority value > 80 (i.e., hardware priority > 5 in the 4-bit scale). This means hardware priority 6–15 (NVIC values 96–240) can safely call FreeRTOS APIs. Priority 0–5 (NVIC 0–80) cannot.

Our UART and button ISRs are set at priority 6 (NVIC value 96 > 80) — correct.

---

## Assert

```c
#define configASSERT(x) \
    do { if ((x) == 0) { vAssertCalled(__FILE__, __LINE__); } } while (0)
```

`configASSERT` is FreeRTOS's internal assertion macro. When an assertion fails (e.g., null pointer passed to a queue function), it calls `vAssertCalled()` defined in `freertos_hooks.cpp`. The `do { } while(0)` wrapper ensures the macro works correctly in all contexts (e.g., in an if-else without braces).

---

## Feature flags

```c
#define configUSE_MUTEXES              0    // not needed
#define configUSE_RECURSIVE_MUTEXES    0    // not needed
#define configUSE_COUNTING_SEMAPHORES  0    // not needed (only binary used)
#define configUSE_TIMERS               0    // not needed (tasks handle timing)
#define configUSE_TICKLESS_IDLE        0    // disable low-power sleep (not needed)
#define configUSE_TRACE_FACILITY       1    // needed for uxTaskGetSystemState
#define configUSE_IDLE_HOOK            0    // no idle hook
#define configUSE_TICK_HOOK            0    // no tick hook
```

Features are disabled if unused — reduces binary size and complexity. `configUSE_TRACE_FACILITY = 1` is needed for `uxTaskGetStackHighWaterMark()` used in telemetry.

---

## API includes (selectively enabled)

```c
#define INCLUDE_vTaskDelayUntil              1  // used by HeartbeatTask
#define INCLUDE_vTaskDelay                   1  // available but not used
#define INCLUDE_xTaskGetSchedulerState       1  // used in SysTick_Handler
#define INCLUDE_uxTaskGetStackHighWaterMark  1  // used in QueueTelemetryTick
```

Each `INCLUDE_*` define compiles a specific FreeRTOS API function into the binary. Disabled functions are not linked. This keeps the binary small — only include what you use.

---

## FreeRTOS + SVC/PendSV aliases

```c
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler
```

FreeRTOS internally uses `vPortSVCHandler` and `xPortPendSVHandler` for its SVC and PendSV interrupt handlers. The STM32 startup file declares these as `SVC_Handler` and `PendSV_Handler`. These macros make the FreeRTOS names equal to the CMSIS names so the linker resolves them correctly.
