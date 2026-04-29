# StateMachine — IDLE / SEARCH / TRACK / FAIL_SAFE

**Files:** `edge/app/state_machine.hpp`, `edge/app/state_machine.cpp`

---

## What this module is

The `StateMachine` class is the core logic of AegisCore. It decides which state the radar system is in and how it responds to events (button presses, simulated detections, gateway commands).

Think of it as a traffic light controller: it knows which light is on, and when a signal arrives (event), it decides which light to switch to next.

---

## State transition diagram

```
          ButtonShortPress
    ┌─────────────────────────►
    │                          SEARCH ──────────────────┐
   IDLE                       │   │                     │
    ▲                         │   │ SimTargetDetected    │
    │   ButtonLongPress        │   ▼                     │
    └──────────────────────── TRACK                      │
    ▲                          │                         │
    │   ButtonLongPress         │ SimTargetLost           │
    └───────────────────────────┘                        │
                                                         │
         ForceFailSafe() / any critical error            │
         ────────────────────────────────────► FAIL_SAFE │
                       (no exit)                         │
                                                         │
         (FAIL_SAFE is terminal — only a reboot exits)   │
```

---

## Class members

### Private state variables

```cpp
SystemState   state_             = SystemState::Idle;    // current state
SystemState   prev_state_        = SystemState::Idle;    // previous state
std::uint32_t search_started_ms_ = 0U;                   // when SEARCH mode started
```

- `state_` and `prev_state_` allow the gateway to know what state we transitioned *from*, which it logs in the event timeline.
- `search_started_ms_` is used to compute the green LED blink phase in `GetLedOutputs()`.

```cpp
static constexpr std::uint32_t kSearchBlinkToggleMs = 500U;
```

The green LED toggles every 500 ms in SEARCH mode. This constant controls that period.

---

## Constructor

```cpp
explicit StateMachine(std::uint32_t initial_timestamp_ms = 0U)
    : search_started_ms_(initial_timestamp_ms)
```

**Purpose:** Create a state machine starting in IDLE state.

The `initial_timestamp_ms` is used to seed `search_started_ms_`. If the system boots directly into SEARCH (which it doesn't currently, but future code might call `ForceState`), the blink phase starts correctly from the provided timestamp.

---

## `Dispatch(const Event& event)` → `bool`

```cpp
bool StateMachine::Dispatch(const Event& event)
```

**Purpose:** Process one event and potentially transition to a new state.

**Returns:** `true` if the event caused a state transition; `false` if it was ignored.

**Logic per state:**

```
IDLE state:
  ButtonShortPress → transition to SEARCH
  anything else    → ignored

SEARCH state:
  ButtonLongPress     → transition to IDLE
  SimTargetDetected   → transition to TRACK
  anything else       → ignored

TRACK state:
  ButtonLongPress     → transition to IDLE
  SimTargetLost       → transition to SEARCH
  anything else       → ignored

FAIL_SAFE state:
  anything → always ignored (terminal state)
```

The `switch` statement follows each state, checking `event.type`. If the event is not handled for the current state, `return false`.

Notice that direct `IDLE → TRACK` is impossible (you must go through SEARCH first). This is an explicit safety requirement from the SRD.

---

## `GetLedOutputs(std::uint32_t timestamp_ms)` → `LedOutputs`

```cpp
LedOutputs StateMachine::GetLedOutputs(const std::uint32_t timestamp_ms) const
```

**Purpose:** Calculate which LEDs should be on at the given moment.

**This is called on every task loop** (approximately every 50–100 ms), not on every state change. The state machine doesn't control GPIO directly — it returns a `LedOutputs` struct, and `ApplyLedOutputs()` in `platform_io.cpp` writes to the hardware.

**SEARCH blink logic:**

```cpp
const std::uint32_t elapsed_ms = timestamp_ms - search_started_ms_;
const bool green_on = ((elapsed_ms / kSearchBlinkToggleMs) % 2U) == 0U;
return LedOutputs{green_on, false};
```

- `elapsed_ms` = how many milliseconds we've been in SEARCH mode
- `elapsed_ms / 500` = how many 500 ms intervals have elapsed (integer division)
- `% 2U == 0` = is the interval count even?
  - Even intervals → LED on (0–499 ms, 1000–1499 ms, ...)
  - Odd intervals → LED off (500–999 ms, 1500–1999 ms, ...)

This produces a steady 1 Hz blink (500 ms on, 500 ms off) with no timer needed.

**Other states:**
- TRACK → `{false, true}` — red on, green off
- IDLE or FAIL_SAFE → `{false, false}` — both off

---

## `TransitionTo(SystemState next_state, std::uint32_t timestamp_ms)`

```cpp
void StateMachine::TransitionTo(const SystemState next_state,
                                const std::uint32_t timestamp_ms) noexcept
```

**Purpose:** Private helper that performs the actual state change. All state changes go through this one function.

**Line by line:**
```cpp
prev_state_ = state_;       // save the old state (for reporting)
state_      = next_state;   // set the new state

if (next_state == SystemState::Search)
{
    search_started_ms_ = timestamp_ms; // reset blink timer
}
```

If we transition to SEARCH, we record the timestamp. This resets the blink phase so the green LED always starts "on" when SEARCH begins (rather than mid-blink).

---

## `ForceFailSafe(std::uint32_t timestamp_ms)`

```cpp
void StateMachine::ForceFailSafe(const std::uint32_t timestamp_ms) noexcept
```

**Purpose:** Force an unconditional transition to FAIL_SAFE from any state.

Unlike `Dispatch()`, this bypasses the transition table — FAIL_SAFE is always reachable from anywhere. This is called by:
- `FailSafeSupervisor` when it detects a critical error
- The gateway `kManualLock` command (operator emergency stop)
- The `kSetState` command with target = FailSafe

---

## `ForceState(SystemState next, std::uint32_t timestamp_ms)`

```cpp
void StateMachine::ForceState(const SystemState next,
                               const std::uint32_t timestamp_ms) noexcept
```

**Purpose:** Force a transition to any specific state, bypassing the normal event-driven table.

Used by the gateway `kSetState` command. Before calling this, `main.cpp` checks that the supervisor is not already in fail-safe mode.

---

## Why is fail-safe a terminal state in code?

`Dispatch()` in FAIL_SAFE always returns `false` — no events are processed. The only exit is `ForceState()`, which can only be called if `FailSafeSupervisor::IsTriggered()` returns false. But once triggered, the supervisor never un-triggers without a reboot. This creates a hardware-reset-only exit from fail-safe, matching the SRD requirement.
