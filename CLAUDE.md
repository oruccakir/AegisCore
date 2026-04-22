# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

AegisCore (Aegis Command & Control) is a demonstration/training full-stack embedded platform that simulates a deterministic, real-time radar state machine. It is intentionally engineered using defense-industry practices (tailored SOLID, MISRA C++ subset, zero-dynamic-memory, fail-safe design, traceability) but is **not** a certified operational system.

The system is designed as three isolated layers (only the Edge Node exists in code today):
1. **Edge Node** — FreeRTOS + C++20 firmware on STM32F407G-DISC1 (Cortex-M4F). IDLE / SEARCH / TRACK state machine, deterministic target-simulation (no real radar).
2. **Gateway Bridge** — Node.js service parsing AC2 UART frames (CRC-16-CCITT + HMAC-SHA-256), broadcasting over WSS.
3. **Command Control Interface** — Next.js 14 dashboard.

Current repo state: **Phase 1a only** — the Edge Node blinks PD12 (green LED) at 500 ms. RTOS, state machine, UART, gateway, and web layers are planned but not yet implemented. Specifications and principles live in `docs/` and `principles/` and precede the code; use them as the source of truth when extending firmware.

## Build / Flash (Edge firmware)

The `arm-none-eabi-` toolchain must be on `PATH`. The vendor HAL lives in a git submodule — run `git submodule update --init --recursive` before the first build.

```bash
# Configure (Ninja + cross toolchain)
cmake -S edge -B edge/build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake

# Build (produces .elf, .bin, .hex + prints section sizes)
cmake --build edge/build

# Flash to STM32F407G-DISC1 via ST-Link
st-flash write edge/build/aegiscore-edge.bin 0x8000000

# Verify ST-Link connectivity
st-info --probe
```

`CMAKE_TOOLCHAIN_FILE` is a path relative to `-S edge`, so it resolves to `edge/cmake/arm-none-eabi.cmake`. Configure always from the repo root as shown — running cmake from inside `edge/` will break that relative path.

Debug vs Release is controlled by the standard `-DCMAKE_BUILD_TYPE=Debug|Release` cache var (`-Og -g3` vs `-Os -g`).

## Build PDFs (specifications)

```bash
cd docs/SRD && latexmk -xelatex srd.tex
cd docs/IRS && latexmk -xelatex ac2-telemetry-irs.tex
```

## Architectural Rules (enforced when adding code)

These come from `principles/*.md` and the SRD — they are non-negotiable in this codebase:

- **No dynamic allocation at runtime.** `new`, `delete`, `malloc`, `free` are prohibited. Objects are global/BSS, stack, or from a fixed-size pool. C++ is compiled with `-fno-exceptions -fno-rtti -fno-threadsafe-statics` (see `edge/CMakeLists.txt`) — respect that: no exceptions, no RTTI, no function-local `static` with non-trivial ctor unless guarded externally.
- **HAL is wrapped, never called from app code.** Application-layer code must depend on abstract C++ interfaces (e.g., `ICommunication`, `ISerial`). `stm32f4xx_hal_*.h` may only be included by a thin wrapper class in BSP/driver layer. `edge/app/main.cpp` is currently Phase 1a and includes HAL directly — when the RTOS + driver layers are introduced, wrap HAL behind interfaces and do not repeat the Phase-1a shortcut.
- **ISRs do not process data.** An ISR sets a flag, gives a semaphore, or pushes to a queue. Observer/consumer tasks do the work.
- **Singletons** for single-instance peripherals must use Meyer's singleton (static local). Remember thread-safe-statics is disabled — initialize these during system bring-up, before other tasks start, not on demand from multiple contexts.
- **Warnings are errors for our code only.** `app/` and `bsp/stm32f4xx_it.c` are compiled with `-Werror`; vendor HAL is not. When adding new first-party files, add them to the `set_source_files_properties(... COMPILE_OPTIONS "-Werror")` list in `edge/CMakeLists.txt`.
- **HAL sources are enumerated explicitly** in `edge/CMakeLists.txt` (`HAL_SOURCES`). When a new phase needs a HAL module (UART, TIM, etc.), append its `.c` to that list rather than glob-including all HAL sources — keeps binary small and dependencies auditable.

## Firmware layout (`edge/`)

- `app/` — application code (`main.cpp` today). First-party, `-Werror`.
- `bsp/` — board support: `stm32f4xx_hal_conf.h` (HAL module enables), `stm32f4xx_it.c` (IRQ handlers), `system_stm32f4xx.c` (CMSIS system init).
- `startup/startup_stm32f407xx.s` — CMSIS reset handler + vector table.
- `linker/STM32F407VGTx_FLASH.ld` — memory map.
- `cmake/arm-none-eabi.cmake` — cross toolchain (Cortex-M4F, `-mfloat-abi=hard`, FPU `fpv4-sp-d16`). Sets `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` so CMake's host-link sanity check does not attempt to run a bare-metal binary.
- `vendor/STM32CubeF4/` — submodule, untouched vendor HAL/CMSIS.

System clock is configured in `SystemClock_Config()` in `app/main.cpp`: HSE 8 MHz → PLL → 168 MHz SYSCLK, AHB 168 / APB1 42 / APB2 84 MHz, Flash 5 WS. Any new peripheral init that depends on bus clocks must assume these frequencies.

## Specifications and traceability

The `docs/` tree is the contractual spec — features/tests trace back to requirement IDs (ARCH-*, FR-*, NFR-*, SR-*, SAF-*, VR-*). When implementing a feature, reference the requirement ID in commit messages / PR descriptions so the traceability matrix in `docs/SRD/sections/10_traceability.tex` stays consistent.

Key documents:
- `docs/SRD/srd.tex` — System Requirements (scope, arch, FR/NFR/SR/SAF/VR, traceability).
- `docs/IRS/ac2-telemetry-irs.tex` — AC2 frame format (CRC-16-CCITT + HMAC-SHA-256) + WebSocket schemas. Read before touching any UART/gateway parser.
- `docs/HAZOP/fmea.md` — H1..H25 hazard log mapped to SAF/SR.
- `docs/VnV/verification-plan.md` — Test strategy (GoogleTest host-side, cppcheck+clang-tidy blocking, pytest HIL, ATS-01..07, 168h soak, libFuzzer on the AC2 parser).

Coverage targets (from the V&V plan) are **line ≥ 90%, branch ≥ 85%, MC/DC ≥ 80%** for critical modules. Static analysis (cppcheck MISRA + clang-tidy cert/cpp-core-guidelines/bugprone/performance) is blocking in CI; deviations must be justified in `docs/deviations.md`.
