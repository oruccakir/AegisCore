# RTOS C++ Development — Expert Learning Path

---

## Where you stand right now

Working on AegisCore you have already touched:

- FreeRTOS tasks, queues, semaphores, static allocation
- Cortex-M4 interrupts, ISR/task boundary rules
- MPU, watchdog, fault handlers
- Binary protocols, CRC, HMAC
- HAL abstraction patterns

That is not beginner territory. You are in the intermediate zone.

---

## Learning path — in order

### 1. FreeRTOS internals (2–3 weeks)

Stop using FreeRTOS as a black box. Read the actual source code.

What to understand:
- How `xTaskCreateStatic` works internally
- How the scheduler picks the next task (the ready list, priority bitmask)
- How context switching works in `PendSV_Handler` — read the ARM assembly
- How `xQueueSend` blocks and unblocks tasks

**Book:** Mastering the FreeRTOS Real Time Kernel — Richard Barry
Free PDF at freertos.org. Read it cover to cover.

---

### 2. Cortex-M4 architecture (2–3 weeks)

You already debugged a VTOR fault. Now go deeper.

What to understand:
- Exception model: priority levels, tail-chaining, late arrival
- The full exception frame layout (r0–r3, r12, lr, pc, xpsr)
- MPU in detail: TEX/S/C/B bits, subregions
- DWT (Data Watchpoint and Trace) for cycle counting
- SysTick internals

**Book:** The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors — Joseph Yiu
This is the bible for Cortex-M development.

---

### 3. Embedded C++ patterns (ongoing)

What to learn:
- CRTP (Curiously Recurring Template Pattern) — zero-cost polymorphism without vtables
- `std::span` for buffer passing instead of pointer + length pairs
- `std::array` everywhere instead of raw C arrays
- Policy-based design
- Type-safe state machines with `std::variant`

**Resources:**
- C++ Core Guidelines (isocpp.github.io/CppCoreGuidelines)
- MISRA C++ 2023 rationale documents

---

### 4. Real-time theory (1–2 weeks)

What to understand:
- Rate Monotonic Scheduling — why task priorities are assigned the way they are
- Worst Case Execution Time (WCET) analysis
- Priority inversion and priority inheritance (why mutexes need it)
- Deadline-driven scheduling

---

### 5. Build things — projects that level you up fast

- Add a real DMA circular buffer to AegisCore (double-buffering)
- Implement a proper task watchdog that monitors all tasks, not just one
- Write a lock-free SPSC (single-producer single-consumer) ring buffer
- Port a module from HAL to register-level — no HAL, raw CMSIS only

---

## The real gap between intermediate and expert

The gap is knowing **what can go wrong**:

| Problem | What it means |
|---------|---------------|
| Race condition between ISR and task | ISR writes a variable the task reads without protection |
| Stack overflow from deep call chains | Function calls too deep, task stack exhausted |
| Priority inversion deadlock | Low-priority task holds a resource a high-priority task needs |
| Watchdog starvation | High-priority task starves the task that feeds the watchdog |
| Clock drift | Using `vTaskDelay` instead of `vTaskDelayUntil` accumulates drift |
| DMA cache coherency | On Cortex-M7, CPU cache and DMA see different memory (not an issue on M4) |

You have already hit one of these in this project — the VTOR/MPU fault. Every bug you debug teaches you more than any book chapter.

---

## What to do next on AegisCore specifically

AegisCore has enough complexity to keep teaching you for months:

- Write the unit tests (`edge/tests/unit/`) — forces you to design for testability
- Fuzz the AC2 parser with libFuzzer — finds edge cases in `AC2Parser::Feed()`
- Add real CPU load measurement using `uxTaskGetSystemState()`
- Wire up the panic block check on boot — send a fault report to the gateway after a crash
- Add a DMA circular buffer so long bursts do not lose bytes

---

## One-line summary

Read the FreeRTOS book, read the Yiu book, then build things and break them.
The bugs you hit are the curriculum.
