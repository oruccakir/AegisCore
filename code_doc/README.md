# AegisCore Edge Firmware — Code Documentation

This directory contains detailed documentation for every source file in `edge/app/` and `edge/bsp/`.

Each document explains **what** each file does, **why** it exists, and **how every function and line of code works** — written for someone who is new to embedded systems.

---

## Index

| File | What it covers |
|------|----------------|
| [01_domain.md](01_domain.md) | Core type definitions shared by the whole system |
| [02_main.md](02_main.md) | Entry point, all FreeRTOS tasks, startup sequence |
| [03_state_machine.md](03_state_machine.md) | IDLE/SEARCH/TRACK/FAIL_SAFE state machine logic |
| [04_simulation_engine.md](04_simulation_engine.md) | Fake target detection/loss events using a PRNG |
| [05_button_classifier.md](05_button_classifier.md) | Converts raw button edge signals into short/long press events |
| [06_platform_io.md](06_platform_io.md) | GPIO (LEDs, button), clock, and HAL initialisation |
| [07_fail_safe_supervisor.md](07_fail_safe_supervisor.md) | Watchdog over heartbeats, CRC/HMAC errors, fault events |
| [08_watchdog.md](08_watchdog.md) | Hardware IWDG watchdog — init and periodic refresh |
| [09_post.md](09_post.md) | Power-On Self-Test: RAM, clock, LED self-test |
| [10_panic.md](10_panic.md) | Hard-fault/MemManage handlers that save crash info to SRAM |
| [11_mpu_config.md](11_mpu_config.md) | Memory Protection Unit — null-guard, Flash RO, SRAM NX |
| [12_crc16.md](12_crc16.md) | CRC-16/CCITT-FALSE checksum for AC2 frames |
| [13_ac2_framer.md](13_ac2_framer.md) | AC2 binary protocol encoder and byte-by-byte parser |
| [14_hmac_sha256.md](14_hmac_sha256.md) | Pure-software SHA-256 + HMAC-SHA-256 implementation |
| [15_replay_guard.md](15_replay_guard.md) | Sequence-number replay attack protection |
| [16_rate_limiter.md](16_rate_limiter.md) | Token-bucket rate limiter for gateway commands |
| [17_telemetry.md](17_telemetry.md) | Command IDs, error codes, and packed payload structs |
| [18_uart_driver.md](18_uart_driver.md) | USART2 + DMA driver implementing ISerial |
| [19_i_serial.md](19_i_serial.md) | Abstract serial interface (ISerial) |
| [20_version.md](20_version.md) | Firmware version constants injected at build time |
| [21_bsp_interrupts.md](21_bsp_interrupts.md) | ISR table, SysTick, FreeRTOS hooks (freertos_hooks.cpp + stm32f4xx_it.c) |
| [22_freertos_config.md](22_freertos_config.md) | FreeRTOS kernel configuration |

---

## How to read these docs

- **Bold** words indicate important concepts.
- `monospace` words refer to exact variable or function names from the code.
- Line numbers refer to the actual source file at the time of writing.
- Every function has a **Purpose**, **Parameters**, **Returns**, and **Line-by-line** section.
