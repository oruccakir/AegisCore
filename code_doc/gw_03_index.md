# Gateway — index.ts (Entry Point)

**File:** `gateway/src/index.ts`

---

## What this file is

The entry point of the gateway process. Node.js starts here when you run `node dist/index.js`.

It does three things:
1. Creates the `Bridge` (which creates the serial and WebSocket subsystems).
2. Registers OS signal handlers for clean shutdown.
3. Calls `bridge.start()` to begin operation.

---

## Full source

```typescript
import { Bridge } from './bridge/Bridge.js';
import { log }    from './config.js';

const bridge = new Bridge();

process.on('SIGINT',  () => void shutdown('SIGINT'));
process.on('SIGTERM', () => void shutdown('SIGTERM'));

async function shutdown(signal: string): Promise<void> {
  log('info', `Received ${signal}, shutting down`);
  try {
    await bridge.stop();
  } finally {
    process.exit(0);
  }
}

bridge.start().catch((err: unknown) => {
  log('error', 'Bridge start failed', err);
  process.exit(1);
});
```

---

## Line by line

```typescript
const bridge = new Bridge();
```

Creates the Bridge object. The Bridge constructor creates the `SerialBridge` and `WsServer` objects but does not open any ports yet. Opening happens in `bridge.start()`.

```typescript
process.on('SIGINT',  () => void shutdown('SIGINT'));
process.on('SIGTERM', () => void shutdown('SIGTERM'));
```

`process.on(signal, handler)` — registers a function to call when the OS sends a signal to this process.

- `SIGINT` — sent when you press Ctrl+C in the terminal.
- `SIGTERM` — sent by `kill <pid>` or by `pkill`. Also sent by Docker/systemd when stopping a service.

`() => void shutdown('SIGINT')` — an arrow function. `void` discards the Promise that `shutdown` returns. Without `void`, some linters would warn about an unhandled Promise. The `void` keyword makes it explicit that we are intentionally not awaiting the result here (the async work is still done).

```typescript
async function shutdown(signal: string): Promise<void> {
  log('info', `Received ${signal}, shutting down`);
  try {
    await bridge.stop();
  } finally {
    process.exit(0);
  }
}
```

`async function` — this function returns a Promise and can use `await` internally. `await bridge.stop()` waits for the serial port and WebSocket server to finish closing before exiting. Without the await, `process.exit(0)` might run before the close operations finish — which would leave file descriptors open.

`finally { process.exit(0) }` — `finally` runs whether `bridge.stop()` succeeds or throws. This guarantees the process exits cleanly even if shutdown fails.

`process.exit(0)` — exits the Node.js process. `0` means "success" (no error). The OS uses this code. `dev.sh` sees the exit code when the gateway process dies.

```typescript
bridge.start().catch((err: unknown) => {
  log('error', 'Bridge start failed', err);
  process.exit(1);
});
```

`bridge.start()` returns a Promise. `.catch()` attaches an error handler — if `start()` rejects (e.g. serial port does not exist, port 8443 is already in use), the catch runs, logs the error, and exits with code `1` (failure). The `dev.sh` script can detect this non-zero exit code.

---

## Why is this file so short?

Good design. All the real logic is in `Bridge`, `SerialBridge`, `WsServer`. `index.ts` only handles the OS-level concerns: process lifecycle, startup, shutdown. Keeping the entry point thin makes the subsystems independently testable.
