# domain.hpp — Core Type Definitions

**File:** `edge/app/domain.hpp`

---

## What this file is

`domain.hpp` is the vocabulary of the whole AegisCore system. It defines the shared types that every other module uses: states, events, LED outputs, and button signals. Nothing here contains logic — it is just data type definitions.

Because every module includes this single header, changing a type here affects everything at once. This is intentional: it is the single source of truth for the system's domain model.

---

## Enum: `SystemState`

```cpp
enum class SystemState : std::uint8_t
{
    Idle,       // 0
    Search,     // 1
    Track,      // 2
    FailSafe    // 3
};
```

**What it is:** Represents the four possible states the radar system can be in.

| Value | Meaning | LEDs |
|-------|---------|------|
| `Idle` | System is powered on but not doing anything. Waiting for a button press. | Both off |
| `Search` | Actively looking for a radar target. Green LED blinks at 500 ms intervals. | Green blinking |
| `Track` | A target has been found and is being tracked. | Red steady on |
| `FailSafe` | A critical error occurred. System is locked. No state transitions allowed. | Both off |

**Why `uint8_t`?** On an embedded system with limited RAM, using a `uint8_t` (1 byte) instead of the default `int` (4 bytes) saves memory.

**Why `enum class`?** `enum class` prevents accidental comparison with integers — you must explicitly cast. This makes bugs easier to catch at compile time.

---

## Enum: `EventType`

```cpp
enum class EventType : std::uint8_t
{
    ButtonShortPress,    // 0
    ButtonLongPress,     // 1
    SimTargetDetected,   // 2
    SimTargetLost        // 3
};
```

**What it is:** The four types of events that can cause the state machine to change state.

| Event | Triggered by | Effect |
|-------|-------------|--------|
| `ButtonShortPress` | User presses button < 500 ms | Idle → Search |
| `ButtonLongPress` | User holds button ≥ 500 ms | Search or Track → Idle |
| `SimTargetDetected` | SimulationEngine randomly fires | Search → Track |
| `SimTargetLost` | SimulationEngine randomly fires | Track → Search |

---

## Struct: `Event`

```cpp
struct Event
{
    EventType type;           // which event happened
    std::uint32_t timestamp_ms; // when it happened (milliseconds since boot)
};
```

**What it is:** A single event that gets dispatched to the state machine.

The `timestamp_ms` field is important because the state machine uses it to know when SEARCH mode started, so it can calculate the green LED blink timing.

---

## Struct: `LedOutputs`

```cpp
struct LedOutputs
{
    bool green_on;  // true = green LED (PD12) should be on
    bool red_on;    // true = red LED (PD14) should be on
};
```

**What it is:** Tells the hardware which LEDs to turn on/off.

The state machine computes this struct every cycle and the main loop applies it to real GPIO pins via `ApplyLedOutputs()`. This separation means the state machine doesn't need to know anything about hardware.

---

## Enum: `RawButtonEdgeType`

```cpp
enum class RawButtonEdgeType : std::uint8_t
{
    Pressed,    // GPIO pin went HIGH
    Released    // GPIO pin went LOW
};
```

**What it is:** A raw electrical signal edge — the pin just changed state. This is not yet a "short press" or "long press" — that classification happens in `ButtonClassifier`.

---

## Struct: `RawButtonEdge`

```cpp
struct RawButtonEdge
{
    RawButtonEdgeType type;        // Pressed or Released
    std::uint32_t timestamp_ms;    // exact time the edge occurred
};
```

**What it is:** A single raw button event, created inside the EXTI interrupt handler and pushed into the button queue. The timestamp is captured at interrupt time so that even if the task is delayed, the timing is accurate.

---

## Key design principle

All these types are in the `aegis::edge` namespace. The `::` means "inside namespace aegis, inside sub-namespace edge." This prevents naming conflicts if the code is ever used alongside other systems.
