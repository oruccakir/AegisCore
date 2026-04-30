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

---

## 8. Understand the gateway (Node.js bridge)

Read the gateway overview first, then follow the data path bottom-up.

- [gw_01_overview.md](gw_01_overview.md) — architecture, data flow diagram, reading order
- [gw_02_config.md](gw_02_config.md) — environment variables, logger
- [gw_03_index.md](gw_03_index.md) — entry point, OS signal handling
- [gw_08_crc16.md](gw_08_crc16.md) — CRC-16/CCITT-FALSE lookup table
- [gw_09_hmac.md](gw_09_hmac.md) — HMAC-SHA-256 (Node.js crypto)
- [gw_07_ac2_framer.md](gw_07_ac2_framer.md) — binary frame encoder (gateway → edge)
- [gw_06_ac2_parser.md](gw_06_ac2_parser.md) — byte-by-byte frame decoder (edge → gateway)
- [gw_05_serial_bridge.md](gw_05_serial_bridge.md) — serialport wrapper
- [gw_10_ws_schemas.md](gw_10_ws_schemas.md) — Zod schemas, WebSocket message types
- [gw_11_ws_server.md](gw_11_ws_server.md) — WebSocket server
- [gw_04_bridge.md](gw_04_bridge.md) — orchestrator (serial ↔ WebSocket)

---

## 9. Understand the UI (Next.js dashboard)

- [ui_01_overview.md](ui_01_overview.md) — architecture, component map, state machine rules
- [ui_02_layout.md](ui_02_layout.md) — root layout, global CSS
- [ui_04_use_ac2_socket.md](ui_04_use_ac2_socket.md) — WebSocket hook (data layer)
- [ui_03_page.md](ui_03_page.md) — main dashboard component
- [ui_05_radar_display.md](ui_05_radar_display.md) — canvas animation
- [ui_06_event_log.md](ui_06_event_log.md) — event log component
