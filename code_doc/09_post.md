# POST — Power-On Self-Test

**Files:** `edge/app/post.hpp`, `edge/app/post.cpp`

---

## What this module is

**POST (Power-On Self-Test)** and **BIT (Built-In-Test)** are standard practices in safety-critical systems. Before a system starts doing its job, it checks that its hardware is working. If anything fails, the system refuses to start.

AegisCore runs POST once during `main()`, before the FreeRTOS scheduler starts. If POST fails, the system spins in an infinite loop and the watchdog eventually resets the MCU.

---

## `POST_Run()` — the main test sequence

```cpp
bool POST_Run() noexcept
{
    if (!FlashCrcTest()) { return false; }
    if (!RamTest())      { return false; }
    if (!IsHseClockReady()) { return false; }
    LedSelfTest();
    return true;
}
```

Runs three checks in order. If any returns `false`, POST immediately returns `false` (short-circuit). The LED self-test always runs last (since it's a visible indicator, not a failure check).

---

## `RamTest()` — RAM write/read pattern test

```cpp
bool RamTest() noexcept
{
    volatile std::uint8_t scratch[1024U];
    for (auto& b : scratch) { b = 0x55U; }
    for (const auto& b : scratch) { if (b != 0x55U) { return false; } }
    for (auto& b : scratch) { b = 0xAAU; }
    for (const auto& b : scratch) { if (b != 0xAAU) { return false; } }
    return true;
}
```

**Purpose:** Verify that a 1 KB region of RAM can correctly store and recall values.

**Why two patterns?**

- `0x55` = `01010101` in binary
- `0xAA` = `10101010` in binary

Together they test every bit in both the '0' and '1' state. A faulty RAM cell might always read as 0 or always read as 1. Testing both patterns catches both types of fault.

**Why `volatile`?**

Without `volatile`, the compiler sees that `scratch` is a local variable written and then read within the same function, and may optimise away the writes entirely ("dead store elimination"). `volatile` forces every write and read to actually happen on the real memory bus.

The array is on the stack — this tests the stack memory region used by this boot context.

---

## `LedSelfTest()` — visual hardware check

```cpp
void LedSelfTest() noexcept
{
    aegis::edge::ApplyLedOutputs({true, false});   // green on for 200 ms
    aegis::edge::DelayMs(200U);
    aegis::edge::ApplyLedOutputs({false, true});   // red on for 200 ms
    aegis::edge::DelayMs(200U);
    aegis::edge::ApplyLedOutputs({false, false});  // both off
}
```

**Purpose:** Turn each LED on and off in sequence so a human observer can verify both LEDs work.

This runs at boot and produces the brief green-then-red flash you see when the board starts. It does not validate anything electronically — it is purely for human verification.

`DelayMs()` is a blocking HAL delay — safe to use here because the scheduler has not started yet.

---

## `FlashCrcTest()` — Flash integrity (stub)

```cpp
bool FlashCrcTest() noexcept
{
    return true;
}
```

**Purpose (future):** Compute a CRC-32 over the entire Flash image and compare against a checksum embedded in the image at link time.

**Why it's a stub now:** The linker script would need to inject `__expected_flash_crc` as a symbol, and CMake would need to run `arm-none-eabi-objcopy` + a checksum tool after linking to patch that symbol in. This infrastructure is on the roadmap but not yet implemented. Returning `true` unconditionally means POST always passes this check.

---

## Why POST must run before the scheduler

The `DelayMs()` call in `LedSelfTest()` uses `HAL_Delay()`, which is a simple busy-wait using the SysTick counter. If the FreeRTOS scheduler were already running, `HAL_Delay()` would still work but `vTaskDelay()` would be the correct approach. More importantly: if POST fails and we spin, it must be simple and deterministic — no tasks, no RTOS state to worry about.
