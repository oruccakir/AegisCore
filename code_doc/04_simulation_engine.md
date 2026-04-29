# SimulationEngine — Fake Target Detection

**Files:** `edge/app/simulation_engine.hpp`, `edge/app/simulation_engine.cpp`

---

## What this module is

AegisCore has no real radar hardware. Instead, the `SimulationEngine` produces fake events that mimic what a real system would generate:
- While in SEARCH mode: occasionally emit `SimTargetDetected` (radar found something)
- While in TRACK mode: occasionally emit `SimTargetLost` (radar lost contact)

These events drive the state machine just as real sensor readings would, making the whole system behave realistically even on a development board.

---

## Why a PRNG (Pseudo-Random Number Generator)?

A PRNG generates numbers that *look* random but are fully deterministic — given the same seed, the sequence is always identical. This is important for embedded systems because:
- No entropy source needed (no hardware random number generator required)
- Reproducible behaviour makes debugging easier
- The output is unpredictable enough for a simulation

The specific algorithm used is called **XorShift** — extremely simple, very fast, and fits in a single 32-bit register.

---

## Constants

```cpp
constexpr std::uint32_t kFallbackSeed = 0x1A2B3C4DU;
```

If the seed provided at construction is 0 (which would break XorShift — all zeros XOR all zeros is still all zeros forever), use this fallback. `main.cpp` passes `0x12345678` so this fallback is never used in practice.

```cpp
constexpr std::uint32_t kSearchDetectDenominator = 50U;  // p = 1/50 = 2%
constexpr std::uint32_t kTrackLossDenominator    = 200U; // p = 1/200 = 0.5%
```

These probabilities are evaluated every 100 ms:
- In SEARCH: 2% chance per 100 ms → on average, a target is detected after ~5 seconds
- In TRACK: 0.5% chance per 100 ms → on average, a target is lost after ~20 seconds

---

## Constructor

```cpp
SimulationEngine::SimulationEngine(const std::uint32_t seed)
    : prng_state_(seed == 0U ? kFallbackSeed : seed)
```

Initialises the PRNG state. The seed determines the entire future sequence — same seed, same simulation.

---

## `Tick100ms(SystemState state, std::uint32_t timestamp_ms)` → `std::optional<Event>`

```cpp
std::optional<Event> SimulationEngine::Tick100ms(const SystemState state,
                                                 const std::uint32_t timestamp_ms)
```

**Purpose:** Called every 100 ms by `StateMachineTask`. May or may not return an event.

`std::optional<Event>` means the function either returns an `Event` or "nothing" (`std::nullopt`). The caller checks with `.has_value()`.

**Logic:**
- If `state == Search`: roll the dice with denominator 50. If it lands (1 in 50 chance), return `SimTargetDetected`.
- If `state == Track`: roll the dice with denominator 200. If it lands (1 in 200 chance), return `SimTargetLost`.
- All other states: return `std::nullopt` (simulation does nothing in IDLE or FAIL_SAFE).

---

## `Roll(std::uint32_t denominator)` → `bool`

```cpp
bool SimulationEngine::Roll(const std::uint32_t denominator)
{
    return (NextRandom() % denominator) == 0U;
}
```

**Purpose:** Return `true` with probability 1/denominator.

`NextRandom() % denominator` produces a value from 0 to denominator-1. Exactly one of those values (0) triggers a `true` result.

Example: `denominator = 50` → true 1 time out of every 50 calls on average.

---

## `NextRandom()` → `std::uint32_t`

```cpp
std::uint32_t SimulationEngine::NextRandom()
{
    std::uint32_t value = prng_state_;
    value ^= value << 13U;  // XOR shift left
    value ^= value >> 17U;  // XOR shift right
    value ^= value << 5U;   // XOR shift left
    prng_state_ = value;
    return value;
}
```

**Purpose:** Advance the PRNG state and return the next pseudo-random number.

This is the **XorShift32** algorithm. Each `^=` with a shift operation mixes bits in the 32-bit state:
1. `value ^= value << 13` — shift left 13, XOR with self. Propagates low bits upward.
2. `value ^= value >> 17` — shift right 17, XOR with self. Propagates high bits downward.
3. `value ^= value << 5`  — shift left 5 again for final mixing.

The three shifts (13, 17, 5) are chosen so the PRNG has a full period of 2³²−1 (every non-zero 32-bit value appears exactly once before repeating). This means the simulation never gets "stuck" producing the same pattern.

**Zero is special:** If `prng_state_` ever becomes 0 (it cannot with this algorithm, but the constructor guards against starting at 0), the PRNG would output 0 forever. That is why the constructor uses `kFallbackSeed` when seed is 0.
