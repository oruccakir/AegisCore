# main.cpp — System Entry Point and FreeRTOS Tasks

**File:** `edge/app/main.cpp`

---

## What this file is

`main.cpp` is the heart of the firmware. It:
1. Initialises all hardware subsystems
2. Creates FreeRTOS queues and tasks (with no dynamic memory)
3. Starts the scheduler

It also contains all four task functions (`UartRxTask`, `StateMachineTask`, `TelemetryTxTask`, `HeartbeatTask`) and the helper functions that support them.

---

## Big picture: what runs when you power on the board

```
power on
  │
  ▼
main()
  ├─ MPU_Init()           — protect memory (null-guard, Flash RO)
  ├─ InitializePlatform() — clock, GPIO
  ├─ FailSafeSupervisor   — create singleton
  ├─ Watchdog::Init()     — start 1-second hardware watchdog
  ├─ POST_Run()           — RAM test, clock check, LED self-test
  ├─ gUart.Init()         — start USART2 + DMA
  ├─ create 3 queues      — buttons, TX frames, remote commands
  ├─ set button callback  — wire EXTI0 ISR to button queue
  ├─ create 4 tasks       — UartRx, StateCore, TelTx, Heartbeat
  └─ vTaskStartScheduler()  ← control never returns from here
```

---

## Constants

### Task priorities and stack sizes

```cpp
constexpr UBaseType_t kPrioUartRx       = 5U;
constexpr UBaseType_t kPrioStateMachine = 4U;
constexpr UBaseType_t kPrioTelemetryTx  = 3U;
constexpr UBaseType_t kPrioHeartbeat    = 2U;
```

Higher number = higher priority. `UartRxTask` has the highest priority because missing incoming bytes would corrupt a frame.

```cpp
constexpr std::uint32_t kStackUartRx       = 512U;   // words (= 2048 bytes)
constexpr std::uint32_t kStackStateMachine = 512U;
constexpr std::uint32_t kStackTelemetryTx  = 512U;
constexpr std::uint32_t kStackHeartbeat    = 256U;   // words (= 1024 bytes)
```

Stack sizes are in **words** (4 bytes each on Cortex-M4). HeartbeatTask needs less stack because it does less work.

### Queue depths

```cpp
constexpr std::uint32_t kButtonQueueLen    = 8U;   // 8 raw button edges can queue up
constexpr std::uint32_t kTxQueueLen        = 4U;   // 4 outgoing frames can queue up
constexpr std::uint32_t kRemoteCmdQueueLen = 4U;   // 4 incoming commands can queue up
```

These are sized so normal bursts don't drop messages, but without wasting RAM.

### Pre-shared key (PSK)

```cpp
constexpr std::uint8_t kPsk[] = {
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF
};
```

This is the 16-byte secret key used to verify HMAC signatures on incoming AC2 frames. In a production system this would come from option bytes or a secure element — here it's a compile-time placeholder (SR-07 roadmap item).

---

## Static allocations (no dynamic memory)

```cpp
StaticTask_t gUartRxTCB, gSMTaskTCB, gTxTaskTCB, gHbTaskTCB;
StackType_t  gUartRxStack[kStackUartRx];
...
```

FreeRTOS tasks normally call `malloc` internally. Because the project rule forbids dynamic allocation, we use `xTaskCreateStatic()` instead — it takes pre-allocated TCB (Task Control Block) and stack buffers. All four tasks' storage lives here as global arrays.

```cpp
StaticQueue_t gButtonQueueCB;
std::array<RawButtonEdge, kButtonQueueLen> gButtonQueueStorage = {};
```

Same principle for queues. `StaticQueue_t` holds the queue's internal bookkeeping; the storage array holds the actual items.

---

## Queue item types

### `TxItem`

```cpp
struct TxItem {
    std::uint8_t cmd;
    std::uint8_t payload[kAC2MaxPayload];
    std::uint8_t len;
};
```

Represents one outgoing AC2 frame waiting to be encoded and transmitted. The `TelemetryTxTask` dequeues these.

### `RemoteCmd`

```cpp
struct RemoteCmd {
    std::uint8_t  cmd;
    std::uint32_t seq;
    std::uint8_t  payload[kAC2MaxPayload];
    std::uint8_t  payload_len;
};
```

Represents one incoming command from the gateway, after HMAC + replay checks have passed. The `StateMachineTask` dequeues and dispatches these.

---

## Helper functions

### `QueueTx`

```cpp
static void QueueTx(std::uint8_t cmd, const std::uint8_t* payload, std::uint8_t len) noexcept
```

**Purpose:** Package a command ID + payload into a `TxItem` and push it to the TX queue.

**Line by line:**
- `TxItem item = {};` — zero-initialise to avoid garbage bytes in unused payload slots
- `item.len = (len <= kAC2MaxPayload) ? len : kAC2MaxPayload;` — clamp to max; never overflow the buffer
- The loop copies each payload byte into `item.payload`
- `xQueueSend(gTxQueue, &item, 0U)` — push to queue, timeout=0 means "don't wait if full" (frame is silently dropped rather than blocking the caller)

### `SendAck`

```cpp
static void SendAck(std::uint32_t echoed_seq) noexcept
```

**Purpose:** Tell the gateway "I received command with sequence number `echoed_seq` successfully."

Fills a `PayloadAck` struct (just the 4-byte sequence number) and calls `QueueTx` with `CmdId::kAck`.

### `SendNack`

```cpp
static void SendNack(std::uint32_t echoed_seq, std::uint8_t err) noexcept
```

**Purpose:** Tell the gateway "I rejected your command — here's why."

Fills a `PayloadNack` struct (sequence number + 1-byte error code) and calls `QueueTx` with `CmdId::kNack`. Error codes are defined in `telemetry.hpp` (`ErrCode::`).

---

## `OnAC2Frame` — the AC2 parser callback

```cpp
static void OnAC2Frame(const AC2Frame& frame, void* /*ctx*/) noexcept
```

**Purpose:** Called by `AC2Parser` each time a complete, structurally valid frame arrives from the gateway. This is called inside `UartRxTask` context (not an ISR).

**Security pipeline — in order:**

1. **Build HMAC input** (lines 141–145): The HMAC covers CMD_ID + PAYLOAD (as defined in IRS §5.3). We concatenate them into `hmac_in`.

2. **HMAC verification** (SR-01, lines 148–158): Call `HMAC_SHA256_Verify()`. If the signature doesn't match, we call `FailSafeSupervisor::Instance().OnHmacFailure()` (counts toward threshold) and send a NACK with error `kAuthFail`. Return immediately — don't process the frame at all.

3. **Replay protection** (SR-02, lines 161–164): Call `gReplay.Check(frame.seq)`. If the sequence number is not strictly increasing, send NACK `kReplay` and return.

4. **Gateway heartbeat** (SAF-04, lines 167–170): If the command is `kHeartbeat`, notify `FailSafeSupervisor` that the gateway is alive and return. Heartbeats don't go to the state machine.

5. **Fail-safe gate** (SAF-02, lines 173–178): If the supervisor has triggered fail-safe, reject all commands with NACK `kFailSafeLock`.

6. **Rate limiting** (SR-05, lines 181–186): For `kSetState` and `kManualLock`, check the token bucket. If empty, send NACK `kRateLimited`.

7. **Forward to state machine** (lines 189–197): Copy the frame into a `RemoteCmd` and push it to `gRemoteCmdQueue` for `StateMachineTask` to process.

---

## `DispatchRemoteCmd` — command dispatcher

```cpp
static void DispatchRemoteCmd(StateMachine& sm, const RemoteCmd& rcmd) noexcept
```

**Purpose:** Runs inside `StateMachineTask`. Takes a validated remote command and applies it to the state machine.

**Cases:**

- **`kSetState`** — payload byte 0 is the target state (0–3). If target is `FailSafe`, call `sm.ForceFailSafe()` and report to supervisor. Otherwise call `sm.ForceState()`. Sends ACK.

- **`kManualLock`** — forces fail-safe unconditionally. This is a "kill switch" from the operator. Sends ACK.

- **`kGetVersion`** — fills a `PayloadVersionReport` with major/minor/patch/git_sha/build_ts from `version.hpp` and queues it for transmission.

- **default** — unknown command ID → NACK `kInvalidCmd`.

---

## `QueueTelemetryTick`

```cpp
static void QueueTelemetryTick(const StateMachine& sm) noexcept
```

**Purpose:** Build a `PayloadTelemetryTick` snapshot and queue it for transmission every second.

- `sm.state()` — current state (0–3)
- `cpu_load_x10 = 0U` — placeholder; actual CPU accounting requires `uxTaskGetSystemState()` which needs a result buffer
- `uxTaskGetStackHighWaterMark(nullptr)` — returns the minimum free stack words for the **calling task** (StateMachineTask). This is the closest watermark to track.
- `HeartbeatMissCount()` — how many seconds since the last gateway heartbeat was received

---

## `OnButtonEdge` — ISR callback

```cpp
static void OnButtonEdge(void* ctx) noexcept
```

**Purpose:** Called from within the EXTI0 interrupt (see `platform_io.cpp`). Must be very fast.

**What it does:**
1. Reads the current button state (`ReadButtonPressed()`) to determine if this is a press or release edge
2. Captures the current timestamp via `MillisecondsSinceBoot()` (reads SysTick — safe from ISR)
3. Pushes a `RawButtonEdge` to the button queue using `xQueueSendFromISR` — the `FromISR` variant is required from interrupt context
4. `portYIELD_FROM_ISR(woken)` — if pushing the queue woke a higher-priority task (StateMachineTask is waiting for button events), yield to it immediately instead of returning to whatever was running

---

## Task: `UartRxTask`

```cpp
void UartRxTask(void* /*ctx*/)
```

**Priority:** 5 (highest of the four tasks)  
**Stack:** 512 words

**Purpose:** Continuously receive bytes from the UART DMA buffer and feed them to the AC2 parser one byte at a time.

**Loop logic:**
1. `gUart.WaitForData()` — blocks (suspended) until the IDLE line interrupt or DMA transfer-complete fires. No CPU is wasted while waiting.
2. `gUart.Read(gUartRxBuf, ...)` — copies bytes from DMA buffer into `gUartRxBuf`, restarts DMA
3. The `for` loop calls `gParser.Feed(byte)` for each byte received. The parser is a state machine that assembles bytes into complete frames; when a frame is complete, it calls `OnAC2Frame`.
4. **CRC error tracking:** `gParser.CrcErrorCount()` returns a running count. We track how many new errors occurred since last loop and call `FailSafeSupervisor::Instance().OnCrcError()` once per error. This feeds the 10-error threshold that triggers fail-safe.

---

## Task: `StateMachineTask`

```cpp
void StateMachineTask(void* /*ctx*/)
```

**Priority:** 4  
**Stack:** 512 words

**Purpose:** The main control task. Feeds the watchdog, checks for fail-safe conditions, dispatches remote commands, processes button events, runs simulation ticks, and updates LEDs.

**Objects created locally:**
- `ButtonClassifier classifier` — debounces button edges and classifies short/long presses
- `StateMachine sm` — the IDLE/SEARCH/TRACK/FAIL_SAFE state machine
- `SimulationEngine sim` — generates fake target detect/lost events

**Loop logic:**

1. **Watchdog feed** — `Watchdog::Feed()` is called every loop iteration. If this task hangs, the watchdog resets the MCU within ~1 second.

2. **Fail-safe check** — If `FailSafeSupervisor::IsTriggered()` is true (set by any error event), force the state machine into FailSafe mode.

3. **Remote commands** — Drain `gRemoteCmdQueue` (non-blocking, `timeout=0`) and dispatch each command to the state machine.

4. **Wait for next simulation tick** — Compute how many ticks until the next 100 ms simulation tick. Block on `gButtonQueue` for that duration. This means the task sleeps efficiently but wakes immediately on a button event.

5. **Button events** — When a button edge arrives, pass it to `classifier.OnEdge()`. If it produces an event (short/long press), dispatch it to the state machine. Drain any additional queued edges without waiting.

6. **Simulation ticks** — Check if one or more 100 ms intervals have elapsed. For each elapsed interval, call `sim.Tick100ms()`. This may return a `SimTargetDetected` or `SimTargetLost` event. This `while` loop catches up if the task was delayed.

7. **Telemetry** — Every 1 second, call `QueueTelemetryTick()` to push a telemetry snapshot to the TX queue.

8. **Apply LEDs** — Call `ApplyLedOutputs(sm.GetLedOutputs(...))` to update GPIO pins based on current state. The state machine handles the blink timing internally.

---

## Task: `TelemetryTxTask`

```cpp
void TelemetryTxTask(void* /*ctx*/)
```

**Priority:** 3  
**Stack:** 512 words

**Purpose:** Dequeue outgoing frames and encode + transmit them over UART.

**Loop logic:**
1. `xQueueReceive(gTxQueue, &item, portMAX_DELAY)` — block indefinitely until there is something to send.
2. `AC2Framer::EncodeTelemetry(...)` — encode the command + payload into a binary AC2 frame (with CRC, zero HMAC). Returns the total byte count.
3. `gTxSeq = gTxSeq + 1U` — increment the outgoing sequence number after each frame.
4. `gUart.Write(encoded, flen)` — transmit the encoded bytes over USART2.

---

## Task: `HeartbeatTask`

```cpp
void HeartbeatTask(void* /*ctx*/)
```

**Priority:** 2 (lowest)  
**Stack:** 256 words

**Purpose:** Send a heartbeat to the gateway every 1 second, and check whether the gateway's heartbeat has timed out.

**Loop logic:**
1. `vTaskDelayUntil(&last_wake, kHeartbeatPeriodTicks)` — precise 1-second period, drift-free (unlike `vTaskDelay` which adds 1 second from *now*, this adds 1 second from the *last wake time*).
2. `CheckHeartbeatTimeout(now_ms)` — checks if the gateway's heartbeat has not been received within 5 seconds; if so, triggers fail-safe.
3. Build and queue a `PayloadHeartbeatTx` (just `uptime_ms`) to let the gateway know the edge node is alive.

---

## `main()` — Entry point

```cpp
extern "C" int main()
```

The `extern "C"` is required because the startup assembly file (`startup_stm32f407xx.s`) calls `main` as a C symbol.

**Step-by-step:**

1. `MPU_Init()` — set up memory protection before anything else. Also sets `SCB->VTOR = FLASH_BASE` as a critical side-effect.

2. `InitializePlatform()` — configure PLL clock to 168 MHz, enable GPIO clocks, configure LED and button pins.

3. `FailSafeSupervisor::Instance()` — force the singleton to construct now (before tasks start), when single-threaded. Required because `-fno-threadsafe-statics` is active.

4. `Watchdog::Init()` — start the hardware IWDG. From this point on, `StateMachineTask` must call `Watchdog::Feed()` every cycle or the MCU resets.

5. `POST_Run()` — runs RAM test, HSE clock check, and LED self-test. If any fail, spin forever (watchdog will reset).

6. `gUart.Init()` — configure USART2 + DMA. If this fails, spin forever.

7. `gParser.SetCallback(OnAC2Frame, nullptr)` — register the frame-complete callback.

8. **Create queues** — all three queues use `xQueueCreateStatic` so no heap is used. `configASSERT` aborts if any queue creation fails.

9. `SetButtonEdgeCallback(OnButtonEdge, gButtonQueue)` — wire the EXTI0 ISR callback to push into `gButtonQueue`.

10. **Create tasks** — all four tasks use `xTaskCreateStatic`. `configASSERT` aborts if any task creation fails.

11. `vTaskStartScheduler()` — hands control to FreeRTOS. Never returns under normal operation.

12. `taskDISABLE_INTERRUPTS(); while (true) {}` — this line is only reached if the scheduler fails (which would mean the kernel ran out of memory for its idle task — impossible here because we use static allocation).
