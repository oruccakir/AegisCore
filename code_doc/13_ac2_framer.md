# AC2 Framer — Binary Protocol Encoder and Parser

**Files:** `edge/app/ac2_framer.hpp`, `edge/app/ac2_framer.cpp`

---

## What this module is

**AC2 (AegisCore Communication v2)** is the binary protocol used between the STM32 edge node and the Node.js gateway over UART. Every message is a fixed-structure binary frame.

This module provides:
- **`AC2Framer`** — encodes (assembles) outgoing frames
- **`AC2Parser`** — decodes (parses) incoming frames byte by byte

---

## Frame format

```
Byte 0:     SYNC      = 0xAA  (marks start of frame)
Byte 1:     VER       = 0x02  (protocol version)
Bytes 2–5:  SEQ       = uint32_t little-endian (sequence number)
Byte 6:     LEN       = uint8_t (payload length, 0–48)
Byte 7:     CMD       = uint8_t (command/event ID)
Bytes 8–55: PAYLOAD   = 0–48 bytes (depends on LEN)
Bytes 56–63: HMAC    = 8 bytes (first 8 bytes of HMAC-SHA-256, or all zeros for telemetry)
Bytes 64–65: CRC     = uint16_t little-endian (CRC-16/CCITT over bytes 0–63)
```

Total frame size: 8 (header) + payload + 8 (HMAC) + 2 (CRC) = 18 + payload, max 66 bytes.

---

## Constants

```cpp
inline constexpr std::uint8_t kAC2Sync        = 0xAAU;    // sync byte
inline constexpr std::uint8_t kAC2Version     = 0x02U;    // protocol version 2
inline constexpr std::uint8_t kAC2MaxPayload  = 48U;      // max payload bytes
inline constexpr std::uint8_t kAC2MaxFrame    = 66U;      // max total frame size
inline constexpr std::uint8_t kAC2HmacLen     = 8U;       // HMAC truncated to 8 bytes
inline constexpr std::uint8_t kAC2HeaderLen   = 8U;       // SYNC+VER+SEQ(4)+LEN+CMD
inline constexpr std::uint8_t kAC2OverheadLen = 18U;      // header + HMAC + CRC
```

---

## `AC2Frame` struct

```cpp
struct AC2Frame
{
    std::uint32_t seq;                      // sequence number
    std::uint8_t  cmd;                      // command ID
    std::uint8_t  payload[kAC2MaxPayload];  // payload bytes
    std::uint8_t  payload_len;              // how many payload bytes are valid
    std::uint8_t  hmac[kAC2HmacLen];        // HMAC bytes
};
```

This is the decoded representation of a received frame, passed to the parser callback.

---

## Encoder: `AC2Framer`

### `BuildHeader` (static, internal)

```cpp
static std::uint8_t BuildHeader(std::uint8_t cmd, std::uint8_t payload_len,
                                 std::uint32_t seq, std::uint8_t* buf) noexcept
```

Writes the 8-byte header into `buf`:

```cpp
buf[0] = kAC2Sync;                                   // 0xAA
buf[1] = kAC2Version;                                // 0x02
buf[2] = seq & 0xFF;                                 // SEQ byte 0 (little-endian)
buf[3] = (seq >> 8) & 0xFF;                          // SEQ byte 1
buf[4] = (seq >> 16) & 0xFF;                         // SEQ byte 2
buf[5] = (seq >> 24) & 0xFF;                         // SEQ byte 3
buf[6] = payload_len;                                // LEN
buf[7] = cmd;                                        // CMD
```

Returns `kAC2HeaderLen` (8) as the number of bytes written.

Little-endian: the least-significant byte of `seq` is written first (index 2), matching how the Node.js gateway parses it with `buf.readUInt32LE(2)`.

### `EncodeTelemetry` (edge → host, no HMAC)

```cpp
static std::uint8_t EncodeTelemetry(std::uint8_t cmd,
                                     const std::uint8_t* payload,
                                     std::uint8_t payload_len,
                                     std::uint32_t seq,
                                     std::uint8_t* out_buf) noexcept
```

**Purpose:** Encode an outgoing telemetry/event frame. HMAC bytes are all zeros because the edge node does not authenticate outgoing messages in the current implementation (only inbound commands are authenticated).

**Line by line:**

```cpp
if (payload_len > kAC2MaxPayload) { payload_len = kAC2MaxPayload; }
```
Clamp payload length — never overflow the buffer.

```cpp
std::uint8_t pos = BuildHeader(cmd, payload_len, seq, out_buf);
for (std::uint8_t i = 0U; i < payload_len; ++i) { out_buf[pos++] = payload[i]; }
```
Write header, then copy payload bytes.

```cpp
(void)std::memset(out_buf + pos, 0U, kAC2HmacLen);
pos += kAC2HmacLen;
```
Zero-fill the HMAC field. `(void)` discards the return value of `memset`.

```cpp
const std::uint16_t crc = CRC16(out_buf, pos);
out_buf[pos++] = crc & 0xFF;   // CRC low byte
out_buf[pos++] = crc >> 8;     // CRC high byte
return pos;
```
Compute CRC over everything written so far (header + payload + HMAC), append as little-endian 16-bit value. Return total frame length.

### `Encode` (with HMAC)

Similar to `EncodeTelemetry` but computes a real HMAC:

```cpp
std::uint8_t body[1U + kAC2MaxPayload];
body[0] = cmd;
memcpy(body + 1U, payload, payload_len);
HMAC_SHA256(hmac_key, hmac_key_len, body, 1U + payload_len, digest);
memcpy(out_buf + pos, digest, kAC2HmacLen);
```

HMAC covers `CMD_ID (1 byte) || PAYLOAD (n bytes)` as specified in IRS §3.1. Only the first 8 bytes of the 32-byte HMAC digest are used (`kAC2HmacLen = 8`).

---

## Parser: `AC2Parser`

The parser is a **state machine that processes one byte at a time**. It assembles bytes into a complete frame and calls a callback when done.

### Parser states

```cpp
enum class State : std::uint8_t
{
    WaitSync,     // looking for 0xAA
    WaitVersion,  // expecting 0x02
    WaitSeq0,     // SEQ byte 0
    WaitSeq1,     // SEQ byte 1
    WaitSeq2,     // SEQ byte 2
    WaitSeq3,     // SEQ byte 3
    WaitLength,   // payload length byte
    WaitCmd,      // command ID byte
    WaitPayload,  // collecting payload bytes
    WaitHmac,     // collecting HMAC bytes
    WaitCrc0,     // CRC low byte
    WaitCrc1,     // CRC high byte → complete frame
};
```

### `SetCallback(FrameCallback cb, void* ctx)`

Registers the function to call when a complete frame is received. Called once during `main()` with `OnAC2Frame`.

### `Reset()`

```cpp
void AC2Parser::Reset() noexcept
```

Clears all parser state (state machine resets to `WaitSync`, counters cleared, frame buffer zeroed). Called automatically when a complete frame is processed or when an error is detected (wrong version byte, oversized length).

### `Feed(std::uint8_t byte)`

```cpp
void AC2Parser::Feed(std::uint8_t byte) noexcept
```

The core function. Called by `UartRxTask` for every byte received from UART.

**State-by-state:**

- **`WaitSync`**: Wait for `0xAA`. Any other byte is silently discarded. When `0xAA` arrives, `Reset()` is called first (clears any previous partial frame), the byte is appended to `raw_[]`, and state advances.

- **`WaitVersion`**: Expect `0x02`. If wrong, `Reset()` and go back to hunting for sync. If correct, advance.

- **`WaitSeq0..3`**: Four bytes forming a 32-bit little-endian sequence number are accumulated in `frame_.seq` using bitshift and OR. Each byte is also appended to `raw_[]` for CRC computation.

- **`WaitLength`**: The payload length byte. If `byte > kAC2MaxPayload` (48), the frame is rejected — SR-08 security requirement prevents processing oversized frames (potential buffer overflow). Otherwise stored in `frame_.payload_len`.

- **`WaitCmd`**: Command ID byte stored in `frame_.cmd`. If `payload_len == 0`, skip `WaitPayload` and go directly to `WaitHmac`.

- **`WaitPayload`**: Collect `frame_.payload_len` bytes into `frame_.payload`. When `payload_idx_ == payload_len`, transition to `WaitHmac`.

- **`WaitHmac`**: Collect 8 HMAC bytes. When all 8 are received, transition to `WaitCrc0`.

- **`WaitCrc0`**: Save the CRC low byte into `rx_crc_lo_` (NOT into `raw_[]` — CRC is not part of the CRC computation).

- **`WaitCrc1`**: Store CRC high byte at `raw_[raw_len_]` (temporarily, one position past the end of the CRC-covered data). Call `Dispatch()`.

### `Dispatch()`

```cpp
void AC2Parser::Dispatch() noexcept
```

Called when all bytes of a frame have been received.

```cpp
const std::uint16_t expected_crc = CRC16(raw_, raw_len_);
const std::uint16_t received_crc = rx_crc_lo_ | (raw_[raw_len_] << 8U);
```

Recompute the CRC over `raw_[0..raw_len_-1]` (everything from SYNC to last HMAC byte). The CRC high byte was stored at `raw_[raw_len_]` by `WaitCrc1`. Reconstruct the received 16-bit CRC.

```cpp
if (expected_crc != received_crc)
{
    ++crc_errors_;
    Reset();
    return;
}
```

CRC mismatch → increment error counter (read by `UartRxTask` to feed `FailSafeSupervisor`) and discard the frame.

```cpp
if (cb_ != nullptr) { cb_(frame_, cb_ctx_); }
Reset();
```

CRC ok → call the registered callback (`OnAC2Frame` in `main.cpp`), then reset for the next frame.

---

## Why `raw_[]` accumulates bytes in parallel with `frame_`

The parser maintains both `frame_` (the decoded struct) and `raw_[]` (the raw bytes). The CRC is computed over raw bytes, not the decoded struct fields. This ensures the CRC check catches any parsing error. `raw_[]` is exactly the bytes from SYNC through the last HMAC byte — the same data that was CRC'd at the sending end.
