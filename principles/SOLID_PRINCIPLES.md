# SOLID Principles for AegisCore (Embedded C++)

## S: Single Responsibility Principle (SRP)

- A class should have one, and only one, reason to change.
- **Example:** A `UartDriver` class must solely handle hardware-level data transmission and reception. It should never be responsible for parsing the incoming data payload.

## O: Open/Closed Principle (OCP)

- Software entities should be open for extension, but closed for modification.
- **Example:** When integrating a new hardware sensor, the existing `SensorManager` code must remain untouched. Instead, implement a new class that inherits from the base sensor interface.

## L: Liskov Substitution Principle (LSP)

- Derived classes must be completely substitutable for their base classes without altering the correctness of the program.
- Overridden virtual functions must strictly adhere to the contract defined by the base interface.

## I: Interface Segregation Principle (ISP)

- No client should be forced to depend on methods it does not use.
- Avoid "fat" interfaces. For instance, split communication interfaces into `ISerial` and `II2C` rather than using a single, monolithic `ICommunication` interface if a module only requires UART.

## D: Dependency Inversion Principle (DIP)

- High-level modules should not depend on low-level modules. Both should depend on abstractions (e.g., interfaces).
- **Critical Rule:** The application layer must never include or call `stm32f4xx_hal_uart.h` directly. It must rely on an abstract `ICommunication` interface, which is then implemented by the hardware-specific wrapper.