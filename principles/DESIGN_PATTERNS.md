# Supported Design Patterns in AegisCore

## 1. Singleton Pattern

- **Use Case:** Managing hardware resources that exist as a single physical instance (e.g., `USART1`, `I2C2`).
- **Implementation:** Ensure thread safety during initialization. Use Meyer's Singleton (static local variable) to guarantee safe instantiation without dynamic allocation.

## 2. Observer Pattern

- **Use Case:** Decoupling Interrupt Service Routines (ISRs) from application logic.
- **Implementation:** An ISR should never process data. It should only set a flag, release a semaphore, or push to an RTOS queue. Observer modules waiting on these RTOS primitives will then wake up and handle the event.

## 3. Strategy Pattern

- **Use Case:** Switching between different data parsing algorithms dynamically (e.g., handling Mavlink vs. a Custom Defense Protocol).
- **Implementation:** Pass a specific parser object (strategy) to the communication handler class at compile-time or initialization.

## 4. Wrapper/Adapter Pattern

- **Use Case:** Converting procedural C code into object-oriented structures.
- **Implementation:** Wrap all STMicroelectronics HAL (Hardware Abstraction Layer) functions inside C++ classes to keep the core logic completely hardware-agnostic.