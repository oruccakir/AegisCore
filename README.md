# Aegis Command & Control🛡️

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![FreeRTOS](https://img.shields.io/badge/RTOS-FreeRTOS-orange.svg)
![Next.js](https://img.shields.io/badge/Web-Next.js-black.svg)
![STM32](https://img.shields.io/badge/Hardware-STM32F407-green.svg)

**Aegis Command & Control** is a full-stack embedded systems project designed to simulate a deterministic, real-time radar state machine with a modern web-based command and control interface.

This project demonstrates the integration of hard real-time C++ embedded systems with modern web technologies via a low-latency WebSocket gateway, strictly adhering to defense-industry standards, SOLID principles, and zero-dynamic-memory allocation constraints on the edge node.

## 🏗️ System Architecture

The system is composed of three isolated but integrated layers:

1. **Edge Node (STM32F407G-DISC1):** The deterministic core. Runs a FreeRTOS-based State Machine (IDLE, SEARCH, TRACK) written in C++20. Handles hardware interrupts and sensor telemetry.
2. **Gateway Bridge:** A lightweight Node.js/Python service that reads the custom AC2 UART Telemetry frames and broadcasts them in real-time to the web layer via WebSockets.
3. **Command Control Interface (Web UI):** A Next.js 14 dashboard providing live radar sweeping visualizations, state logs, and remote command execution.