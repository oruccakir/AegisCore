# BSP Interrupts — ISR Table, SysTick, FreeRTOS Hooks

**Files:** `edge/bsp/stm32f4xx_it.c`, `edge/app/freertos_hooks.cpp`

---

## `stm32f4xx_it.c` — Interrupt Service Routines

This file contains the interrupt handlers that the CPU jumps to when hardware events fire. The vector table (in `startup_stm32f407xx.s`) maps each interrupt number to one of these function names.

---

### `NMI_Handler`

```c
void NMI_Handler(void) { while (1) { } }
```

Non-Maskable Interrupt — fired for very severe hardware errors (clock failure, Flash ECC error). Simply spins; the watchdog will reset.

---

### `DebugMon_Handler`

```c
void DebugMon_Handler(void) { }
```

Debug monitor exception — used by debuggers (ST-Link). Empty because no special handling is needed.

---

### `EXTI0_IRQHandler`

```c
void EXTI0_IRQHandler(void)
{
    Aegis_HandleExti0Irq();
}
```

Fires when PA0 (User Button) changes state (rising or falling edge). Calls `Aegis_HandleExti0Irq()` defined in `platform_io.cpp`, which calls `HAL_GPIO_EXTI_IRQHandler()`, which clears the pending flag and calls `HAL_GPIO_EXTI_Callback()`, which invokes `OnButtonEdge()`.

This chain keeps `stm32f4xx_it.c` as a thin router — no logic here.

---

### `USART2_IRQHandler`

```c
void USART2_IRQHandler(void)
{
    UartDriver_OnUsart2Irq();
}
```

Fires for USART2 interrupts (IDLE line detection, errors). Forwards to the driver's handler which checks the IDLE flag and calls `SignalRxData()`.

---

### `DMA1_Stream5_IRQHandler`

```c
void DMA1_Stream5_IRQHandler(void)
{
    UartDriver_OnDma1Stream5Irq();
}
```

Fires when the DMA transfer complete (TC) flag is set — i.e., when the DMA has filled the entire `rx_dma_buf_`. Forwards to `HAL_DMA_IRQHandler()` which calls the HAL receive callback.

---

### `SysTick_Handler`

```c
void SysTick_Handler(void)
{
    HAL_IncTick();

    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        xPortSysTickHandler();
    }
}
```

SysTick fires every 1 ms (configured by `HAL_Init()` which sets up the SysTick timer based on the CPU clock).

`HAL_IncTick()` — increments the HAL tick counter used by `HAL_GetTick()` and `HAL_Delay()`. This must be called first, before the FreeRTOS handler.

`xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED` — during the boot sequence before `vTaskStartScheduler()` is called, calling `xPortSysTickHandler()` would crash (FreeRTOS data structures not yet initialised). This guard prevents that.

`xPortSysTickHandler()` — FreeRTOS's SysTick handler. Increments the kernel tick count, and may trigger a context switch (preemption) if a higher-priority task has become ready.

**Why is SysTick shared?** HAL needs a 1ms tick for `HAL_Delay()` and `HAL_GetTick()`. FreeRTOS also needs the SysTick for its scheduler tick. They share the same SysTick interrupt — HAL gets served first, then FreeRTOS.

---

### Fault handlers

The comment in `stm32f4xx_it.c` notes:

```c
// Fault handlers are in bsp/fault_stubs.c (naked trampolines → Panic_HardFaultImpl).
```

`HardFault_Handler`, `MemManage_Handler`, `BusFault_Handler`, and `UsageFault_Handler` are defined in `fault_stubs.c` (not here) to keep the naked assembly isolated. See [10_panic.md](10_panic.md) for details.

---

## `freertos_hooks.cpp` — FreeRTOS Application Hooks

FreeRTOS defines several "hook" functions that the application can implement. These are weak symbols — if the application doesn't define them, the linker uses a default (usually an infinite loop or no-op).

---

### `vApplicationGetIdleTaskMemory`

```cpp
extern "C" void vApplicationGetIdleTaskMemory(StaticTask_t** ppxIdleTaskTCBBuffer,
                                              StackType_t** ppxIdleTaskStackBuffer,
                                              uint32_t* pulIdleTaskStackSize)
{
    static StaticTask_t idle_task_tcb;
    static StackType_t  idle_task_stack[configMINIMAL_STACK_SIZE];

    *ppxIdleTaskTCBBuffer  = &idle_task_tcb;
    *ppxIdleTaskStackBuffer = idle_task_stack;
    *pulIdleTaskStackSize  = configMINIMAL_STACK_SIZE;
}
```

Because `configSUPPORT_STATIC_ALLOCATION = 1` and `configSUPPORT_DYNAMIC_ALLOCATION = 0`, FreeRTOS requires the application to provide memory for the Idle task (which runs when no other task is ready). This function gives FreeRTOS pointers to static storage.

`configMINIMAL_STACK_SIZE = 128` words (512 bytes) — sufficient for the Idle task which does very little.

Function-local statics are safe here because this function is called exactly once by the FreeRTOS kernel during `vTaskStartScheduler()`, before any tasks run — single-threaded context, so `-fno-threadsafe-statics` is not a problem.

---

### `vApplicationStackOverflowHook`

```cpp
extern "C" void vApplicationStackOverflowHook(TaskHandle_t /*xTask*/,
                                              char* pcTaskName)
{
    auto* pb = aegis::edge::GetPanicBlock();
    if (pb != nullptr)
    {
        (void)std::strncpy(pb->task_name, pcTaskName,
                           sizeof(pb->task_name) - 1U);
        pb->task_name[sizeof(pb->task_name) - 1U] = '\0';
    }

    aegis::edge::FailSafeSupervisor::Instance().ReportEvent(
        aegis::edge::FailSafeEvent::StackOverflow);

    taskDISABLE_INTERRUPTS();
    while (true) { }
}
```

Called by FreeRTOS when a task's stack has overflowed. This requires `configCHECK_FOR_STACK_OVERFLOW = 2` in `FreeRTOSConfig.h` (SAF-05 requirement).

**Line by line:**

`GetPanicBlock()` — get the pointer to the `.noinit` panic block.

`strncpy(pb->task_name, pcTaskName, ...)` — save the name of the overflowing task into the panic block. `strncpy` is used instead of `strcpy` to prevent buffer overrun. The `- 1U` leaves room for the null terminator.

`pb->task_name[sizeof - 1U] = '\0'` — guarantee null termination even if `pcTaskName` is longer than 15 characters.

`FailSafeSupervisor::Instance().ReportEvent(StackOverflow)` — trigger the fail-safe. The `StateMachineTask` will detect this on its next cycle.

`taskDISABLE_INTERRUPTS(); while (true) {}` — disable interrupts and spin. The watchdog will reset the MCU within ~1 second. The panic block is preserved through the reset (`.noinit` section).

---

### `vAssertCalled`

```cpp
extern "C" void vAssertCalled(const char* /*file*/, int /*line*/)
{
    taskDISABLE_INTERRUPTS();
    while (true) { }
}
```

Called when `configASSERT(x)` fails. This happens when FreeRTOS detects an internal consistency error (e.g., null queue handle passed to `xQueueSend`). Disable interrupts and spin — watchdog resets.

The `file` and `line` arguments are discarded. In a more advanced system, these could be written to the panic block. Currently the watchdog reset + empty `.noinit` block signals to the next boot that a failed assert occurred (indistinguishable from a watchdog timeout).
