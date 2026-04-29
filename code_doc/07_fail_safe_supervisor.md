# FailSafeSupervisor — System Health Monitor

**Files:** `edge/app/fail_safe_supervisor.hpp`, `edge/app/fail_safe_supervisor.cpp`

---

## What this module is

`FailSafeSupervisor` is the system's guardian. It watches for signs of danger and decides when to lock the system into fail-safe mode — a state from which no normal commands can escape.

It monitors:
- **Gateway heartbeat** — if the gateway stops sending heartbeats, the link is dead
- **CRC errors** — too many corrupted frames on the wire means something is wrong
- **HMAC failures** — too many authentication failures could indicate an attacker
- **Stack overflow** — if a FreeRTOS task exhausts its stack, the system is unstable
- **HardFault** — a CPU exception occurred
- **POST failure** — the power-on self-test found a hardware defect

Once triggered, `IsTriggered()` returns `true` forever until the MCU is reset. There is no "un-trigger."

---

## Singleton pattern

```cpp
static FailSafeSupervisor& Instance() noexcept
{
    static FailSafeSupervisor instance;
    return instance;
}
```

This is **Meyer's singleton** — a local static inside a function. The object is created the first time `Instance()` is called and lives for the program's lifetime.

**Why local static?** `new` is forbidden (no dynamic allocation). A global static would work too, but then construction order between translation units is undefined. Meyer's singleton ensures the object is constructed before the first use.

**Why is this safe here?** The project compiles with `-fno-threadsafe-statics`, which means the compiler does NOT generate lock/unlock code around the `static` initialisation. This is safe because `main()` calls `FailSafeSupervisor::Instance()` once before any tasks start, during single-threaded initialisation. After that, tasks only call it after the singleton already exists.

---

## Private state

```cpp
volatile bool         triggered_       = false;
volatile bool         degraded_        = false;
volatile std::uint32_t last_hb_ms_     = 0U;
volatile std::uint8_t  hb_miss_count_  = 0U;
volatile std::uint8_t  crc_err_count_  = 0U;
volatile std::uint8_t  hmac_fail_count_= 0U;
```

All state variables are `volatile`. This tells the compiler "do not optimise reads/writes to this variable — it may change at any time." This is necessary because ISRs and tasks from different priority levels may read/write these flags.

- `triggered_` — the master fail-safe flag. Once true, stays true.
- `degraded_` — a softer warning: link is slow but not yet dead.
- `last_hb_ms_` — timestamp of the last gateway heartbeat received.
- `hb_miss_count_` — how many seconds since last heartbeat (reported in telemetry).
- `crc_err_count_` — running CRC error count (resets on trigger).
- `hmac_fail_count_` — running HMAC failure count.

### Thresholds

```cpp
static constexpr std::uint32_t kDegradedThresholdMs  = 3000U;  // 3 seconds without HB = degraded
static constexpr std::uint32_t kFailSafeThresholdMs  = 5000U;  // 5 seconds without HB = fail-safe
static constexpr std::uint8_t  kMaxCrcErrors         = 10U;    // 10 CRC errors = fail-safe
static constexpr std::uint8_t  kMaxHmacFailures      = 5U;     // 5 HMAC failures = fail-safe
```

---

## `ReportEvent(FailSafeEvent event)`

```cpp
void FailSafeSupervisor::ReportEvent(FailSafeEvent /*event*/) noexcept
{
    triggered_ = true;
    degraded_  = true;
}
```

**Purpose:** Called by anyone who detects a critical failure. Sets both `triggered_` and `degraded_` to `true`. The event type is currently unused (logged by the caller) — in a future version it could be stored in the panic block.

This function is used by: stack overflow hook, HardFault handler, POST failure, heartbeat timeout, CRC/HMAC threshold.

---

## `CheckHeartbeatTimeout(std::uint32_t now_ms)`

```cpp
void FailSafeSupervisor::CheckHeartbeatTimeout(std::uint32_t now_ms) noexcept
```

**Purpose:** Called by `HeartbeatTask` every 1 second. Checks how long since the last gateway heartbeat arrived.

**Logic:**

```cpp
if (triggered_) { return; }
```
Once fail-safe is triggered, no point monitoring further.

```cpp
if (last_hb_ms_ == 0U)
{
    last_hb_ms_ = now_ms;
    return;
}
```
If we have never received a heartbeat (`last_hb_ms_` is still 0 from initialisation), seed the timer with the current time and wait. This prevents a false fail-safe trigger at boot when the gateway hasn't connected yet.

```cpp
const std::uint32_t elapsed = now_ms - last_hb_ms_;
```
Time since last heartbeat, in milliseconds.

```cpp
if (elapsed >= kFailSafeThresholdMs)
{
    hb_miss_count_ = ...elapsed / 1000...; // seconds elapsed, capped at 255
    ReportEvent(FailSafeEvent::HeartbeatLoss);
}
else if (elapsed >= kDegradedThresholdMs)
{
    hb_miss_count_ = ...;
    degraded_ = true;
}
else
{
    degraded_ = false;  // link is healthy
}
```

Three tiers:
- < 3 s: link is healthy, `degraded_=false`
- 3–5 s: link is slow, `degraded_=true` (warns in telemetry but no fail-safe yet)
- ≥ 5 s: fail-safe triggered

---

## `OnGatewayHeartbeatReceived(std::uint32_t now_ms)`

```cpp
void FailSafeSupervisor::OnGatewayHeartbeatReceived(std::uint32_t now_ms) noexcept
{
    last_hb_ms_    = now_ms;    // reset the timer
    hb_miss_count_ = 0U;        // clear miss count
    degraded_      = false;     // clear degraded flag
}
```

**Purpose:** Called by `OnAC2Frame` in `main.cpp` when a valid `kHeartbeat` frame arrives.

Resets the heartbeat timer. Clears `degraded_` and `hb_miss_count_`. Does NOT clear `triggered_` — if fail-safe was already set by something else, receiving a heartbeat doesn't undo it.

---

## `OnCrcError()`

```cpp
void FailSafeSupervisor::OnCrcError() noexcept
{
    if (triggered_) { return; }
    crc_err_count_ = crc_err_count_ + 1U;
    if (crc_err_count_ >= kMaxCrcErrors)
    {
        ReportEvent(FailSafeEvent::CrcErrorThreshold);
    }
}
```

**Purpose:** Called by `UartRxTask` once per CRC error detected by the AC2 parser.

Increments the error counter. On the 10th error, triggers fail-safe. The counter is not reset on errors — it accumulates. This means 10 CRC errors total (not 10 in a row) triggers fail-safe.

---

## `OnHmacFailure()`

```cpp
void FailSafeSupervisor::OnHmacFailure() noexcept
{
    if (triggered_) { return; }
    hmac_fail_count_ = hmac_fail_count_ + 1U;
    if (hmac_fail_count_ >= kMaxHmacFailures)
    {
        ReportEvent(FailSafeEvent::HmacFailure);
    }
}
```

**Purpose:** Called by `OnAC2Frame` in `main.cpp` whenever HMAC verification fails.

Same pattern as `OnCrcError` but with threshold 5. Five authentication failures trigger fail-safe, protecting against brute-force attacks.

---

## Public query methods

```cpp
[[nodiscard]] bool IsTriggered() const noexcept { return triggered_; }
[[nodiscard]] bool IsDegraded()  const noexcept { return degraded_; }
[[nodiscard]] std::uint8_t HeartbeatMissCount() const noexcept { return hb_miss_count_; }
```

Used by:
- `IsTriggered()` — `StateMachineTask` polls this every loop and forces the state machine into FailSafe if true
- `IsDegraded()` — currently unused in the task, available for future telemetry warning flag
- `HeartbeatMissCount()` — included in the telemetry tick sent to the gateway every second

---

## `FailSafeEvent` enum

```cpp
enum class FailSafeEvent : std::uint8_t
{
    StackOverflow,
    HardFault,
    HeartbeatLoss,
    CrcErrorThreshold,
    HmacFailure,
    PostFailure,
    ExternalTrigger,
};
```

Each caller reports which type of event caused the fail-safe. Currently all events have the same effect (set `triggered_=true`), but the enum is preserved for future use in fault reporting or audit logs.
