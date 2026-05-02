# FreeRTOS Task Model in AegisCore

This note explains how tasks are created, scheduled, switched, and coordinated in the AegisCore STM32 firmware.

The target MCU is the STM32F407, which has a single Cortex-M4 CPU core. FreeRTOS gives the firmware multiple independent task contexts, but only one task executes instructions on the CPU at any instant. The system appears concurrent because the scheduler switches between tasks quickly and deterministically.

## Core Idea

A FreeRTOS task is a function plus execution context:

- a function entry point,
- a task control block, usually called a TCB,
- a private stack,
- a priority,
- a state such as running, ready, blocked, or deleted.

In this firmware, system tasks are created with `xTaskCreateStatic()`, and user tasks are also created with `xTaskCreateStatic()` from a fixed slot table.

Dynamic allocation is disabled:

```c
#define configSUPPORT_DYNAMIC_ALLOCATION        0
#define configSUPPORT_STATIC_ALLOCATION         1
```

This matters for embedded determinism. The firmware does not ask the heap for task memory at runtime. Every task stack and TCB is statically reserved in `.bss` or static storage before the scheduler starts.

## Scheduler Configuration

The key scheduler settings live in `edge/bsp/FreeRTOSConfig.h`.

```c
#define configUSE_PREEMPTION                    1
#define configTICK_RATE_HZ                      1000U
#define configMAX_PRIORITIES                    8
#define configSUPPORT_DYNAMIC_ALLOCATION        0
#define configSUPPORT_STATIC_ALLOCATION         1
```

Meaning:

- Preemption is enabled.
- The RTOS tick runs at 1000 Hz, so one tick is 1 ms.
- There are 8 priority levels.
- Static allocation is required.
- Dynamic allocation is disabled.

Preemption means that if a higher-priority task becomes ready, FreeRTOS can interrupt the currently running lower-priority task and switch to the higher-priority one.

## Single Core Execution

All tasks run on the same Cortex-M4 core.

That means this is not true hardware parallelism:

```text
Task A and Task B do not execute instructions at the exact same time.
```

Instead, execution looks like this:

```text
time ---->

CPU:  UartRx  | StateCore | RangeScan | TelTx | Idle | StateCore | ...
```

Only one block is running at any instant. FreeRTOS saves the current task context, restores another task context, and resumes that task from where it previously stopped.

## Task Priorities

System task priorities are defined in `edge/app/main.cpp`:

```cpp
constexpr UBaseType_t kPrioUartRx       = 5U;
constexpr UBaseType_t kPrioStateMachine = 4U;
constexpr UBaseType_t kPrioTelemetryTx  = 3U;
constexpr UBaseType_t kPrioHeartbeat    = 2U;
```

User tasks are created at priority `1U`.

The priority order is therefore:

```text
Priority 5: UartRx
Priority 4: StateCore
Priority 3: TelTx
Priority 2: Heartbeat
Priority 1: User tasks such as Blink and RangeScan
Priority 0: Idle task
```

Higher numbers mean higher priority. If `UartRx` is ready, it can preempt `RangeScan`. If all higher-priority tasks are blocked or delayed, a lower-priority task can run.

## System Task Creation

System tasks are created near the end of `main()` in `edge/app/main.cpp`.

The pattern is:

```cpp
gUartRxHandle = xTaskCreateStatic(
    UartRxTask,
    "UartRx",
    kStackUartRx,
    nullptr,
    kPrioUartRx,
    gUartRxStack,
    &gUartRxTCB);
```

Each task gets:

- the task function,
- a short name,
- stack depth,
- optional context pointer,
- priority,
- stack storage,
- TCB storage.

After all system tasks and queues are created, the scheduler starts:

```cpp
vTaskStartScheduler();
```

After this call, FreeRTOS owns scheduling. Normal code execution no longer proceeds linearly like a basic `while (true)` bare-metal loop.

## Static Stack and TCB Storage

The firmware statically declares system task memory:

```cpp
StaticTask_t gUartRxTCB, gSMTaskTCB, gTxTaskTCB, gHbTaskTCB;

StackType_t gUartRxStack[kStackUartRx];
StackType_t gSMStack[kStackStateMachine];
StackType_t gTxStack[kStackTelemetryTx];
StackType_t gHbStack[kStackHeartbeat];
```

The TCB stores task metadata. The stack stores the task's call frames, local variables, saved registers, and return context.

During a context switch, FreeRTOS saves the outgoing task's CPU context to that task's stack and later restores it. This is why each task needs its own stack.

## User Task Slots

User-created tasks use a fixed slot table:

```cpp
struct UserTaskSlot {
    StaticTask_t  tcb;
    StackType_t   stack[256U];
    TaskHandle_t  handle;
    UserTaskType  task_type;
    std::uint8_t  param;
    bool          in_use;
};

constexpr std::uint8_t kUserTaskSlots = 4U;
UserTaskSlot gUserTasks[kUserTaskSlots] = {};
```

The firmware can create up to 4 user tasks. Each slot owns:

- one TCB,
- one 256-word stack,
- one task handle,
- task type,
- task parameter,
- in-use flag.

This avoids runtime heap allocation and keeps the system predictable.

## User Task Types

The current user task enum is:

```cpp
enum class UserTaskType : std::uint8_t {
    Blink     = 0U,
    RangeScan = 3U
};
```

The UI, gateway, LLM command layer, and firmware all agree on these numeric task IDs. Task IDs `1` and `2` are intentionally unused; older `Counter` and `Load` demo tasks were removed because they did not serve the current hardware workflow.

## Creating a User Task

User tasks are created by a Host to Gateway to STM32 command flow.

At a high level:

```text
UI or LLM
  -> WebSocket command
  -> Gateway
  -> AC2 UART frame
  -> STM32 UartRxTask
  -> Remote command queue
  -> StateMachineTask
  -> CreateUserTask()
```

The command payload is:

```cpp
struct PayloadCreateTask {
    std::uint8_t task_type;
    std::uint8_t param;
};
```

`CreateUserTask()` finds the first free slot, selects the function for the requested task type, stores the parameter, and calls `xTaskCreateStatic()`.

Simplified flow:

```cpp
for each slot:
    if slot is free:
        choose task function by task_type
        fill task name
        store task_type and param
        call xTaskCreateStatic(...)
        mark slot as in_use
        return slot index
```

The created task does not necessarily run immediately. It becomes ready. The scheduler decides when it runs based on priority and whether other tasks are ready.

## Deleting a User Task

User tasks are deleted with:

```cpp
vTaskDelete(gUserTasks[slot_index].handle);
```

Then the firmware clears the slot:

```cpp
gUserTasks[slot_index].handle = nullptr;
gUserTasks[slot_index].in_use = false;
```

For `RangeScan`, deletion also returns the servo to a neutral position:

```cpp
SetServoAngleDegrees(90U);
```

The task's static memory is not freed because it was never heap-allocated. The slot simply becomes available for a later task.

## Task States

FreeRTOS tasks move through states:

```text
Ready    : task can run when selected by scheduler
Running  : task is currently executing on the CPU
Blocked  : task is waiting for time, queue data, or an event
Deleted  : task has been removed
```

Most well-behaved RTOS tasks spend much of their time blocked, not running.

Examples:

```cpp
vTaskDelay(pdMS_TO_TICKS(100U));
```

This blocks the current task for 100 ms.

```cpp
xQueueReceive(gTxQueue, &item, portMAX_DELAY);
```

This blocks until there is data in the queue.

Blocking is good. It gives CPU time to other tasks and avoids wasteful busy loops.

## When Does Switching Happen?

FreeRTOS can switch tasks when:

- the running task calls `vTaskDelay()`,
- the running task waits on a queue,
- a tick interrupt occurs,
- an ISR wakes a higher-priority task,
- a higher-priority task becomes ready,
- the running task is deleted.

Because preemption is enabled, a lower-priority task does not get to keep the CPU if a higher-priority task becomes ready.

## Context Switching on Cortex-M

On Cortex-M, FreeRTOS uses exception machinery for context switching. The key handlers are mapped in `FreeRTOSConfig.h`:

```c
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler
```

Conceptually, a context switch does this:

```text
1. Save current task registers to its stack.
2. Store that task's stack pointer in its TCB.
3. Choose the next ready task.
4. Load the next task's stack pointer from its TCB.
5. Restore that task's registers.
6. Return from exception into the new task.
```

The task continues exactly where it previously yielded or was preempted.

## Queues as Communication Primitives

This firmware mainly uses queues for inter-task communication.

The queues are statically allocated:

```cpp
gButtonQueue
gTxQueue
gRemoteCmdQueue
```

They are created with `xQueueCreateStatic()`.

Important queue flows:

```text
Button EXTI ISR
  -> gButtonQueue
  -> StateMachineTask
```

```text
UartRxTask
  -> parses AC2 frame
  -> gRemoteCmdQueue
  -> StateMachineTask
```

```text
Any task wanting telemetry
  -> QueueTx()
  -> gTxQueue
  -> TelemetryTxTask
  -> UART write
```

This keeps ownership clean. Instead of many tasks writing UART directly, tasks enqueue transmit items and `TelemetryTxTask` performs the actual write.

## Why Queues Instead of Shared Variables?

Queues provide:

- ordering,
- bounded capacity,
- blocking behavior,
- safe handoff between tasks and ISR contexts,
- less need for ad hoc shared state.

Shared variables are still used in a few small places, but the main command and telemetry flow is queue-based.

For example, `QueueTx()` sends a telemetry item:

```cpp
xQueueSend(gTxQueue, &item, 0U);
```

`TelemetryTxTask` receives it:

```cpp
xQueueReceive(gTxQueue, &item, portMAX_DELAY);
```

The receive call blocks until there is work to do.

## RangeScan as a Concrete Example

`RangeScan` is a user task. It is not always running. It starts only when the UI or LLM sends a create-task command with `task_type = 3`.

Its parameter is the near-target threshold in centimeters. For example:

```text
task_type = 3
param = 30
```

This means:

```text
Create a RangeScan task that locks when the HC-SR04 distance is <= 30 cm.
```

The task loop roughly does this:

```text
set servo angle
delay for servo settling
every few angle steps, measure distance
if target is near:
    lock at current angle
    keep measuring
    release lock after several lost samples
else:
    continue sweep
send range scan telemetry
delay/yield
```

The important scheduling detail is that RangeScan calls `vTaskDelay()` often. It does not monopolize the CPU. While the servo is settling, other tasks can run.

## Is RangeScan Running at the Same Time as UART?

Logically yes, physically no.

The user experience is:

```text
servo moves
HC-SR04 measures
UART telemetry streams
UI updates
heartbeat continues
```

Internally this is time-sliced and priority-driven:

```text
RangeScan runs briefly, then delays.
TelemetryTx wakes when gTxQueue has data.
UartRx wakes when UART data arrives.
StateCore wakes periodically or when commands arrive.
Heartbeat wakes once per second.
```

All of these share the same CPU core.

## Blocking vs Busy Work

Good task design:

```cpp
for (;;) {
    do_small_work();
    vTaskDelay(period);
}
```

Risky task design:

```cpp
for (;;) {
    while (true) {
        do_work_without_delay();
    }
}
```

A task that never blocks can starve lower-priority tasks. If it has high priority, it can damage the whole system's responsiveness. User tasks in this firmware should therefore do bounded work and call `vTaskDelay()` or wait on a queue/event regularly.

## Tick Rate and Time

The tick rate is 1000 Hz:

```c
#define configTICK_RATE_HZ 1000U
```

So:

```text
1 tick = 1 ms
```

The macro:

```cpp
pdMS_TO_TICKS(100U)
```

converts milliseconds to RTOS ticks. With this config, 100 ms becomes 100 ticks.

## Runtime Stats

Runtime stats are enabled:

```c
#define configUSE_TRACE_FACILITY      1
#define configGENERATE_RUN_TIME_STATS 1
```

The firmware uses `uxTaskGetSystemState()` to build task telemetry. That is how the UI can show:

- task name,
- state,
- priority,
- stack high-water mark,
- CPU load estimate,
- user task slot ID.

This does not create tasks. It only observes scheduler state.

## Stack High-Water Mark

The task monitor reports stack watermark values. A high-water mark is the minimum amount of unused stack observed so far.

If it becomes very small, the task has nearly exhausted its stack. That is dangerous because stack overflow can corrupt memory.

The config enables stack overflow checking:

```c
#define configCHECK_FOR_STACK_OVERFLOW 2
```

This helps catch stack problems during runtime.

## Why No Heap-Based Tasks?

The repository guidelines avoid dynamic allocation in firmware. FreeRTOS follows that design:

```c
#define configSUPPORT_DYNAMIC_ALLOCATION 0
```

Benefits:

- predictable memory use,
- no heap fragmentation,
- no runtime allocation failure path for tasks,
- easier safety analysis,
- clearer maximum task count.

Tradeoff:

- the maximum number of user tasks is fixed,
- each task slot reserves stack memory even when unused.

## Summary

AegisCore uses FreeRTOS as a deterministic cooperative/preemptive task runtime on a single Cortex-M4 core.

The key model is:

```text
Separate stacks, separate task contexts, one CPU core.
Scheduler chooses one ready task at a time.
Priorities decide who wins when multiple tasks are ready.
Queues move data between tasks.
Delays and queue waits let other tasks run.
Static allocation keeps memory predictable.
```

For this codebase, the most important practical rule is:

```text
Do small bounded work, then block or delay.
```

That is what lets UART, telemetry, heartbeat, state machine logic, and user tasks like RangeScan coexist smoothly on one embedded CPU.
