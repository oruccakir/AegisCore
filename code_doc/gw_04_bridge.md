# Gateway — Bridge.ts (Orchestrator)

**File:** `gateway/src/bridge/Bridge.ts`

---

## What this file is

`Bridge` is the central coordinator of the gateway. It owns references to both the serial layer and the WebSocket layer, and routes data between them. It is the only place that knows about both sides.

Think of it as a translator sitting in the middle:

```
SerialBridge ──onSerialFrame──► Bridge.onSerialFrame() ──► WsServer.broadcast()
WsServer     ──onCommand──────► Bridge.onWsCommand()   ──► SerialBridge.write()
```

---

## State maps

```typescript
const STATE_NAME: Record<number, string> = {
  0: 'idle', 1: 'search', 2: 'track', 3: 'fail_safe',
};
const STATE_NUM: Record<string, number> = {
  idle: 0, search: 1, track: 2, fail_safe: 3,
};
```

Two lookup tables for converting between the numeric state IDs (used in the binary protocol) and the string names (used in JSON). `Record<number, string>` means "an object whose keys are numbers and values are strings."

---

## Class fields

```typescript
private serial:    SerialBridge;
private ws:        WsServer;
private psk:       Buffer;
private txSeq      = 0;
private hbTimer:   ReturnType<typeof setInterval> | null = null;
```

`private` — TypeScript access modifier. These fields can only be read or written from inside `Bridge` methods. External code cannot access them.

`psk: Buffer` — the Pre-Shared Key as raw bytes. Stored as a `Buffer` (Node.js byte array) converted from the hex string in `Config.pskHex`. Buffer is more efficient for crypto operations than a hex string.

`txSeq = 0` — outgoing sequence number. Incremented by `this.txSeq++` each time a frame is sent. The edge uses this for replay detection — it rejects any frame whose seq is not greater than the last seen seq.

`hbTimer: ReturnType<typeof setInterval> | null` — the return type of `setInterval` is a timer handle. `ReturnType<typeof setInterval>` extracts that type automatically — no need to write `NodeJS.Timeout` manually. `| null` means it can be null before the timer is created.

---

## constructor

```typescript
constructor() {
  this.serial = new SerialBridge(Config.serialPort, Config.serialBaud);
  this.ws     = new WsServer(Config.wsHost, Config.wsPort);
  this.psk    = Buffer.from(PSK_HEX, 'hex');
}
```

`Buffer.from(PSK_HEX, 'hex')` — converts the 32-character hex string `'DEADBEEFCAFEBABE0123456789ABCDEF'` into a 16-byte Buffer. `'hex'` encoding means every two hex characters become one byte.

The constructor does not open any ports — that happens in `start()`. This separation allows the Bridge object to be created synchronously before any async I/O.

---

## start()

```typescript
async start(): Promise<void> {
  this.serial.on('frame', (f: AC2Frame) => this.onSerialFrame(f));
  this.serial.on('error', (e: Error)   => log('error', 'serial error', e.message));
  this.ws.on('command', (cmd: InboundCmd, seq: number) => this.onWsCommand(cmd, seq));

  await this.serial.open();

  this.hbTimer = setInterval(() => this.sendHeartbeat(), HB_INTERVAL_MS);
  log('info', 'Bridge started');
}
```

`this.serial.on('frame', ...)` — subscribes to the `'frame'` event. Every time `SerialBridge` emits a complete AC2 frame (after CRC passes), `onSerialFrame` is called.

`await this.serial.open()` — opens the serial port. This is async because the OS must allocate the file descriptor for the serial device. The `await` pauses execution until the port is open or an error is thrown.

`setInterval(() => this.sendHeartbeat(), HB_INTERVAL_MS)` — schedules `sendHeartbeat` to run every 1000 ms. Returns a timer handle stored in `hbTimer` so we can cancel it later in `stop()`.

---

## stop()

```typescript
async stop(): Promise<void> {
  if (this.hbTimer) clearInterval(this.hbTimer);
  await this.serial.close();
  await this.ws.close();
}
```

`clearInterval(this.hbTimer)` — cancels the heartbeat timer. Without this, the timer would fire after `close()` and try to write to an already-closed serial port.

`await this.serial.close()` then `await this.ws.close()` — close both I/O channels. The `await` ensures each one finishes before moving to the next. Order matters: close serial first (stop incoming data) then close WebSocket (stop outgoing data).

---

## onSerialFrame() — edge → browser

```typescript
private onSerialFrame(frame: AC2Frame): void {
  if (!frame.crcOk) {
    log('warn', 'CRC error dropped', { ... });
    return;
  }

  const hasHmac = frame.hmac.some((b) => b !== 0);
  if (hasHmac && !hmacVerify(this.psk, frame.cmd, frame.payload, frame.hmac)) {
    log('warn', 'HMAC verify failed', { ... });
    return;
  }
  ...
  switch (frame.cmd) { ... }
}
```

**CRC check:** `frame.crcOk` is set by `AC2Parser` during frame assembly. If the two CRC bytes received from UART do not match the CRC computed over the rest of the frame, the frame is silently dropped.

**HMAC check:** `frame.hmac.some((b) => b !== 0)` — checks if any HMAC byte is non-zero. Edge telemetry frames have all-zero HMAC (firmware fills them with 0x00). The gateway trusts these without HMAC verification. Only frames with a real HMAC (non-zero) get verified.

**The switch statement:** Maps binary `CmdId` values to JSON event objects for the browser. For example:

```typescript
case CmdId.TelemetryTick:
  this.ws.broadcast({
    type:                 'evt.telemetry',
    state:                frame.payload[0] ?? 0,
    cpu_load_x10:         frame.payload.readUInt16LE(1),
    free_stack_min_words: frame.payload.readUInt16LE(3),
    hb_miss_count:        frame.payload[5] ?? 0,
  });
  break;
```

`frame.payload.readUInt16LE(1)` — reads a 16-bit unsigned integer at byte offset 1, in little-endian byte order (least significant byte first). This matches how the STM32 stores multi-byte values (`uint16_t` on ARM is naturally little-endian).

`frame.payload[0] ?? 0` — reads byte at index 0. The `?? 0` default handles the case where `payload` is unexpectedly empty (TypeScript requires this because array access can return `undefined`).

---

## onWsCommand() — browser → edge

```typescript
private onWsCommand(cmd: InboundCmd, _seq: number): void {
  switch (cmd.type) {
    case 'cmd.set_state': {
      const payload = Buffer.alloc(1);
      payload[0] = STATE_NUM[cmd.targetState] ?? 0;
      this.serial.write(encodeCommand(CmdId.SetState, payload, this.txSeq++, this.psk));
      break;
    }
    ...
  }
}
```

`Buffer.alloc(1)` — allocates a 1-byte buffer initialized to zeros. This is the payload for `SetState` — a single byte holding the target state number (0–3).

`this.txSeq++` — post-increment: uses the current value of `txSeq` as the sequence number, then increments it for the next call. The edge's replay guard requires each command to have a strictly increasing sequence number.

`encodeCommand(CmdId.SetState, payload, seq, psk)` — builds the full binary AC2 frame (header + payload + HMAC + CRC) and returns it as a Buffer. See `gw_07_ac2_framer.md`.

---

## sendHeartbeat()

```typescript
private sendHeartbeat(): void {
  const payload = Buffer.alloc(4);
  payload.writeUInt32LE((Date.now() & 0xffffffff) >>> 0, 0);
  this.serial.write(encodeCommand(CmdId.Heartbeat, payload, this.txSeq++, this.psk));
}
```

`Date.now()` — returns the current time as milliseconds since the Unix epoch (Jan 1 1970). This is a JavaScript `number` (a 64-bit float), which can hold integers up to 2^53 exactly.

`& 0xffffffff` — bitwise AND with `0xFFFFFFFF`. In JavaScript, bitwise operators work on 32-bit integers. This masks to the low 32 bits of the timestamp.

`>>> 0` — unsigned right shift by 0 bits. This is a JavaScript trick to convert a signed 32-bit integer to an unsigned 32-bit integer. Without it, if bit 31 is set, the value would be negative.

`payload.writeUInt32LE(value, 0)` — writes the 32-bit value to the buffer at offset 0, in little-endian byte order.

The edge compares this timestamp against its internal `HAL_GetTick()` to detect if the gateway is still alive. Heartbeat loss ≥ 5 consecutive triggers fail-safe.
