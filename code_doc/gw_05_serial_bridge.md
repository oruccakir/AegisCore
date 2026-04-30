# Gateway — SerialBridge.ts

**File:** `gateway/src/serial/SerialBridge.ts`

---

## What this file is

`SerialBridge` wraps the `serialport` npm package and the `AC2Parser`. It turns the raw byte stream from the serial port into parsed `AC2Frame` objects, and provides a `write()` method for sending binary frames.

It extends `EventEmitter` so it can emit `'frame'` and `'error'` events that `Bridge` subscribes to.

---

## Dependencies

**`serialport`** — A Node.js library for communicating with serial (UART/COM) ports. It handles opening the OS device file (e.g. `/dev/ttyACM0`), reading bytes asynchronously, and writing bytes. It hides the differences between Linux, macOS, and Windows serial port APIs.

**`EventEmitter`** — A built-in Node.js class. Objects that extend `EventEmitter` can call `this.emit('eventName', data)` and external code can subscribe with `.on('eventName', handler)`. This is the classic Node.js event-driven pattern.

---

## Constructor

```typescript
constructor(path: string, baudRate: number) {
  super();
  this.parser = new AC2Parser();
  this.parser.onFrame((frame) => this.emit('frame', frame));

  this.port = new SerialPort({ path, baudRate, autoOpen: false });

  this.port.on('data', (chunk: Buffer) => {
    this.parser.feedBuffer(chunk);
  });

  this.port.on('error', (err: Error) => {
    log('error', 'SerialPort error', err.message);
    this.emit('error', err);
  });

  this.port.on('close', () => {
    log('warn', 'SerialPort closed');
    this.emit('close');
  });
}
```

`super()` — calls the `EventEmitter` constructor. Required when extending a class.

`this.parser.onFrame((frame) => this.emit('frame', frame))` — registers a callback on the parser. When AC2Parser completes a frame, it calls this callback, which re-emits the frame as a `'frame'` event on `SerialBridge` itself. `Bridge` listens to this event. This is event forwarding — the parser fires, SerialBridge forwards it up.

`new SerialPort({ path, baudRate, autoOpen: false })` — creates the port object but does not open it yet. `autoOpen: false` prevents the library from immediately trying to open the device on construction. Opening happens explicitly in `open()`.

`this.port.on('data', (chunk: Buffer) => { this.parser.feedBuffer(chunk); })` — the `'data'` event fires whenever new bytes arrive from the serial device. `chunk` is a Buffer containing however many bytes arrived in this read (could be 1, could be 30 — depends on OS buffering). `feedBuffer` loops through the chunk and calls `feed(byte)` for each byte.

---

## open() and close()

```typescript
open(): Promise<void> {
  return new Promise((resolve, reject) => {
    this.port.open((err) => {
      if (err) { reject(err); return; }
      log('info', `Serial port opened`, { path: this.port.path });
      resolve();
    });
  });
}
```

`new Promise((resolve, reject) => { ... })` — the `serialport` library uses callbacks (the older Node.js pattern), not Promises. This wraps it in a Promise so the caller can use `await`. If `open()` fails (device not found, permission denied), `reject(err)` causes the Promise to reject, which propagates to `bridge.start()` and then to `index.ts` which exits the process.

```typescript
close(): Promise<void> {
  return new Promise((resolve) => {
    if (!this.port.isOpen) { resolve(); return; }
    this.port.close(() => resolve());
  });
}
```

`this.port.isOpen` — a boolean property on the SerialPort object. Checked first to avoid trying to close an already-closed port (which would throw).

---

## write()

```typescript
write(frame: Buffer): void {
  log('debug', 'serial TX', { bytes: frame.length, cmd: `0x${frame[7]!.toString(16).padStart(2,'0')}` });
  this.port.write(frame, (err) => {
    if (err) log('error', 'Serial write error', err.message);
    else     log('debug', 'serial TX done');
  });
}
```

`frame[7]!` — byte index 7 is the CMD byte in the AC2 frame header (see `gw_07_ac2_framer.md` for the frame layout). The `!` is a TypeScript non-null assertion — tells the compiler "I know this is not undefined" because the frame always has at least 8 header bytes.

`.toString(16).padStart(2,'0')` — converts a number to hex string. `(16).toString(16)` → `"10"`. `padStart(2,'0')` ensures at least 2 digits: `(5).toString(16).padStart(2,'0')` → `"05"`.

`this.port.write(frame, callback)` — sends the buffer to the serial port. The write is buffered — the callback fires when the bytes have been handed to the OS, not necessarily when they have been physically transmitted at 115200 baud.

---

## Getter properties

```typescript
get crcErrors(): number  { return this.parser.crcErrors; }
get frameCount(): number { return this.parser.frameCount; }
```

`get` — TypeScript/JavaScript getter. These allow `serialBridge.crcErrors` to be read like a field, even though it's delegating to `this.parser.crcErrors`. Callers don't need to know that `AC2Parser` is the source.

Used in `Bridge.onSerialFrame()` for logging CRC error counts.
