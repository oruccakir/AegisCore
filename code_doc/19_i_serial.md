# ISerial — Abstract Serial Interface

**File:** `edge/app/i_serial.hpp`

---

## What this file is

`ISerial` is a pure abstract interface (C++ abstract base class) that defines how to write and read bytes over a serial connection.

`UartDriver` in `edge/bsp/` implements this interface for USART2. Application code (tasks in `main.cpp`) uses `ISerial*` — it only knows the interface, not the concrete implementation.

---

## The interface

```cpp
class ISerial
{
public:
    virtual ~ISerial() = default;

    virtual bool Write(const std::uint8_t* data, std::uint8_t len) noexcept = 0;

    virtual std::uint8_t Read(std::uint8_t* dst, std::uint8_t max_len) noexcept = 0;
};
```

### `Write(data, len)` → `bool`

- **Blocking write** — sends `len` bytes from `data` over the serial link
- Returns `true` on success, `false` on timeout or hardware error
- Application code (TelemetryTxTask) calls this after encoding an AC2 frame

### `Read(dst, max_len)` → `uint8_t`

- **Non-blocking read** — copies available bytes into `dst`, up to `max_len`
- Returns the number of bytes actually copied (0 if nothing is ready)
- Application code (UartRxTask) calls this after `WaitForData()` signals data is ready

---

## Why a virtual interface?

The architectural rule (ARCH-05) says: "application code must depend on abstract interfaces, not on HAL directly."

Benefits:
1. **Testability** — unit tests can substitute a `MockSerial` class instead of needing real UART hardware
2. **Portability** — if the UART pin changes, only `UartDriver` changes; tasks are untouched
3. **Clarity** — the interface documents what operations are available without exposing HAL internals

---

## Why `noexcept`?

The codebase compiles with `-fno-exceptions`. C++ exceptions are entirely disabled. `noexcept` makes this explicit in the interface contract — callers don't need to worry about exception propagation.

---

## `virtual ~ISerial() = default`

Virtual destructor: required whenever a class has virtual methods and will be deleted through a base pointer. Without it, deleting a `UartDriver*` cast as `ISerial*` would leak the derived class's resources.

In this codebase there is no dynamic allocation so no object is ever `delete`d, but the virtual destructor is still declared for correctness and to silence compiler warnings.
