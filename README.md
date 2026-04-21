# Aegis Command & Control🛡️

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![FreeRTOS](https://img.shields.io/badge/RTOS-FreeRTOS-orange.svg)
![Next.js](https://img.shields.io/badge/Web-Next.js-black.svg)
![STM32](https://img.shields.io/badge/Hardware-STM32F407G--DISC1-green.svg)

**Aegis Command & Control** is a full-stack embedded-systems demonstration platform that simulates a deterministic, real-time radar state machine with a modern web-based command and control interface.

This project showcases the integration of hard real-time C++ embedded systems with modern web technologies via a low-latency WebSocket gateway. It applies defense-industry engineering practices (tailored SOLID, MISRA C++ subset, zero-dynamic-memory allocation, fail-safe design, traceability) as a **demonstration / training** artifact — not as a certified operational system.

## System Architecture

The system is composed of three isolated but integrated layers:

1. **Edge Node:** The deterministic core. Runs a FreeRTOS-based State Machine (IDLE, SEARCH, TRACK) written in C++20. Handles hardware interrupts and a deterministic target-simulation engine (no physical radar).
2. **Gateway Bridge:** A lightweight Node.js service that parses the custom AC2 UART telemetry frames (CRC-16-CCITT + HMAC-SHA-256) and broadcasts them in real-time to the web layer via WebSocket Secure (wss://).
3. **Command Control Interface:** A Next.js 14 dashboard providing live radar sweep visualizations, state logs, and remote command execution.

## Documentation Index

Specification artifacts are maintained under `docs/`:

| Document | Path | Purpose |
| --- | --- | --- |
| System Requirements Document (SRD) | `docs/SRD/srd.tex` → `srd.pdf` | ARCH, FR, NFR, SR, SAF, VR, traceability. |
| Interface Requirements Specification (IRS) | `docs/IRS/ac2-telemetry-irs.tex` | AC2 frame format + WebSocket schemas. |
| Hazard Analysis & FMEA | `docs/HAZOP/fmea.md` | Hazard log H1..H25 mapped to SAF/SR requirements. |
| Verification & Validation Plan | `docs/VnV/verification-plan.md` | Test levels, tools, entry/exit criteria. |
| Design Principles | `principles/*.md` | SOLID, OOP, design patterns guidance. |

Build the SRD PDF: `cd docs/SRD && latexmk -xelatex srd.tex`.
