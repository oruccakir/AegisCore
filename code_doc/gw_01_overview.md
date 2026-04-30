# Gateway — Architecture Overview

**Directory:** `gateway/src/`

---

## What the gateway is

The gateway is a Node.js service written in TypeScript. It sits between the STM32F407 edge firmware and the web-based Command & Control Interface (CCI).

```
STM32F407 ──USB/UART──► SerialPort ──► AC2Parser ──► Bridge ──► WsServer ──► Browser (CCI)
                                                        │
                                     AC2Framer ◄────────┘◄──── WsServer ◄──── Browser (CCI)
```

The gateway does two things:
1. **Decode** — receive binary AC2 frames from the edge over UART, verify CRC and HMAC, translate to JSON WebSocket events.
2. **Encode** — receive JSON commands from the browser, sign them with HMAC-SHA-256, encode to AC2 binary frames, write to UART.

---

## File map

| File | Role |
|------|------|
| `index.ts` | Entry point — creates Bridge, handles shutdown signals |
| `config.ts` | Runtime config from environment variables + logger |
| `bridge/Bridge.ts` | Orchestrator — wires serial ↔ WebSocket, heartbeat timer |
| `serial/SerialBridge.ts` | Node.js `serialport` wrapper + `AC2Parser` integration |
| `serial/AC2Parser.ts` | Byte-by-byte state machine that assembles AC2 frames |
| `serial/AC2Framer.ts` | Frame encoder — builds binary AC2 frames from cmd+payload |
| `serial/Crc16.ts` | CRC-16/CCITT-FALSE lookup table, matches edge firmware |
| `serial/Hmac.ts` | HMAC-SHA-256 (truncated to 8 bytes) using Node.js `crypto` |
| `ws/schemas.ts` | Zod schemas for WebSocket message validation + TypeScript types |
| `ws/WsServer.ts` | `ws` WebSocket server — accept clients, broadcast events, parse commands |

---

## Data flow: edge → browser

1. Edge sends a binary AC2 frame over UART (USART2 at 115200 baud).
2. `SerialBridge` receives raw bytes from `serialport`'s `data` event.
3. Bytes are fed one-by-one into `AC2Parser.feed()`.
4. When a complete frame is assembled, `AC2Parser` fires `onFrame`.
5. `Bridge.onSerialFrame()` checks CRC, optionally checks HMAC.
6. `Bridge` maps the `CmdId` to a JSON event object and calls `WsServer.broadcast()`.
7. `WsServer` wraps the event in the standard envelope `{ v:2, type, ts, seq, data }` and sends to all connected clients.

## Data flow: browser → edge

1. Browser sends a JSON command wrapped in the envelope.
2. `WsServer.handleMessage()` parses and validates it with Zod.
3. `WsServer` emits `'command'` event.
4. `Bridge.onWsCommand()` maps command type to a `CmdId`, builds a payload `Buffer`.
5. `encodeCommand()` in `AC2Framer.ts` builds the binary frame with HMAC and CRC.
6. `SerialBridge.write()` sends it to the STM32.

---

## Security model

- **Outgoing commands (gateway → edge)** are signed with a full HMAC-SHA-256 truncated to 8 bytes, using the pre-shared key (PSK) from `Config.pskHex`.
- **Incoming telemetry (edge → gateway)** has zeroed HMAC fields — the edge does not sign telemetry to save CPU cycles. The gateway detects zero HMAC and skips verification.
- **Incoming command responses (edge → gateway)** also use zero HMAC by firmware convention.
- If a non-zero HMAC is present and does not match, the frame is dropped.

---

## Reading order

1. [gw_02_config.md](gw_02_config.md) — configuration and logger
2. [gw_03_index.md](gw_03_index.md) — entry point
3. [gw_08_crc16.md](gw_08_crc16.md) — checksum (no dependencies)
4. [gw_09_hmac.md](gw_09_hmac.md) — authentication (no dependencies)
5. [gw_07_ac2_framer.md](gw_07_ac2_framer.md) — frame encoder
6. [gw_06_ac2_parser.md](gw_06_ac2_parser.md) — frame decoder
7. [gw_05_serial_bridge.md](gw_05_serial_bridge.md) — serial port integration
8. [gw_10_ws_schemas.md](gw_10_ws_schemas.md) — WebSocket types
9. [gw_11_ws_server.md](gw_11_ws_server.md) — WebSocket server
10. [gw_04_bridge.md](gw_04_bridge.md) — main orchestrator
