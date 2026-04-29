# Watchdog — Hardware Reset Safety Net

**Files:** `edge/app/watchdog.hpp`, `edge/bsp/watchdog.cpp`

---

## What this module is

The STM32F407 has a hardware timer called the **IWDG (Independent Watchdog)**. It counts down from a preset value. If software doesn't reset it before it reaches zero, the hardware automatically resets the entire MCU.

This is a critical safety feature: if the `StateMachineTask` crashes or hangs (infinite loop, deadlock, etc.), the watchdog fires and the MCU reboots into a known state.

The IWDG is "independent" because it runs from its own internal 32 kHz oscillator (LSI). Even if the main clock fails, the watchdog still fires.

---

## Timeout calculation

```
LSI frequency:   ~32,000 Hz
Prescaler:       /32
IWDG clock:      32,000 / 32 = 1,000 Hz (1 ms per tick)
Reload value:    1023
Timeout:         (1023 + 1) / 1000 Hz = 1.024 seconds
```

This means `StateMachineTask` must call `Watchdog::Feed()` at least once every 1.024 seconds. Since the task loops at ~100 ms (simulation tick period), it feeds the watchdog approximately 10 times per second — well within the limit.

---

## `Watchdog::Init()`

```cpp
void Watchdog::Init() noexcept
{
    g_hiwdg.Instance       = IWDG;
    g_hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    g_hiwdg.Init.Reload    = 1023U;
    (void)HAL_IWDG_Init(&g_hiwdg);
}
```

**Line by line:**

- `g_hiwdg.Instance = IWDG` — point the HAL handle at the IWDG peripheral register block
- `IWDG_PRESCALER_32` — divide the 32 kHz LSI by 32 → 1 kHz watchdog clock
- `Reload = 1023` — the counter starts at 1023 and counts down to 0 → 1024 ticks = 1.024 s
- `HAL_IWDG_Init(&g_hiwdg)` — writes the prescaler and reload to the IWDG peripheral, starts the counter. The `(void)` discards the return value since failure here is non-recoverable.

**Important:** Once `HAL_IWDG_Init()` is called, the watchdog is running. There is no way to stop the IWDG in hardware — it runs until reset. This is intentional.

---

## `Watchdog::Feed()`

```cpp
void Watchdog::Feed() noexcept
{
    (void)HAL_IWDG_Refresh(&g_hiwdg);
}
```

`HAL_IWDG_Refresh` writes the reload value back to the IWDG counter register, resetting the countdown. This must be called before the 1.024 s timeout expires.

Only `StateMachineTask` calls this (at the top of its main loop). If any other task causes a problem that starves `StateMachineTask`, the watchdog fires. This makes `StateMachineTask` the system's "heartbeat" in code.

---

## HAL handle: `g_hiwdg`

```cpp
static IWDG_HandleTypeDef g_hiwdg;
```

A file-scope static — hidden from other translation units. HAL uses this as a context object to know which peripheral it is managing. It stores the prescaler, reload, and a state machine used by HAL internally.

---

## Why the header has no HAL includes

`watchdog.hpp` declares the class without any HAL types. `watchdog.cpp` includes `stm32f4xx_hal.h`. This separation means application code can include `watchdog.hpp` without pulling in hundreds of HAL headers. This is the "HAL stays in BSP" rule.
