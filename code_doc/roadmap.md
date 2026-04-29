# Reading Roadmap

Read the documentation files in this order. Each one builds on the previous.

---

## 1. Understand the vocabulary first

- [01_domain.md](01_domain.md) — the types everything else uses (states, events, LEDs)

---

## 2. Understand the hardware layer

- [06_platform_io.md](06_platform_io.md) — how clock, GPIO, and button work at the hardware level
- [22_freertos_config.md](22_freertos_config.md) — what the RTOS kernel is configured to do
- [08_watchdog.md](08_watchdog.md) — the hardware safety net

---

## 3. Understand the core logic

- [03_state_machine.md](03_state_machine.md) — the main business logic (IDLE/SEARCH/TRACK/FAIL_SAFE)
- [05_button_classifier.md](05_button_classifier.md) — how a physical button press becomes a state event
- [04_simulation_engine.md](04_simulation_engine.md) — how fake radar events are generated

---

## 4. Understand the safety systems

- [09_post.md](09_post.md) — what happens before anything starts
- [11_mpu_config.md](11_mpu_config.md) — memory protection (read the VTOR bug story here)
- [07_fail_safe_supervisor.md](07_fail_safe_supervisor.md) — the guardian that watches everything
- [10_panic.md](10_panic.md) — what happens when the CPU crashes

---

## 5. Understand the communication protocol

- [17_telemetry.md](17_telemetry.md) — the command/event dictionary (read this before framer)
- [12_crc16.md](12_crc16.md) — checksum math
- [13_ac2_framer.md](13_ac2_framer.md) — how binary frames are built and parsed byte by byte
- [14_hmac_sha256.md](14_hmac_sha256.md) — how authentication works
- [15_replay_guard.md](15_replay_guard.md) — replay attack protection
- [16_rate_limiter.md](16_rate_limiter.md) — rate limiting

---

## 6. Understand the driver layer

- [19_i_serial.md](19_i_serial.md) — the abstract interface
- [18_uart_driver.md](18_uart_driver.md) — the concrete USART2+DMA driver

---

## 7. Finally, read the entry point — it all connects here

- [02_main.md](02_main.md) — the full startup sequence and all 4 tasks

---

## Supporting files (read any time after step 3)

- [20_version.md](20_version.md) — CMake-injected git SHA and build timestamp
- [21_bsp_interrupts.md](21_bsp_interrupts.md) — ISR table, SysTick, FreeRTOS hooks
