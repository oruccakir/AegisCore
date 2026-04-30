# Gateway — config.ts

**File:** `gateway/src/config.ts`

---

## What this file is

Two things live here:
1. `Config` — a single object that holds every runtime setting the gateway needs.
2. `log()` — the one logging function used everywhere in the gateway.

---

## Config object

```typescript
export const Config = {
  serialPort:   process.env['SERIAL_PORT']   ?? '/dev/ttyUSB0',
  serialBaud:   Number(process.env['SERIAL_BAUD'] ?? '115200'),
  wsPort:       Number(process.env['WS_PORT']  ?? '8443'),
  wsHost:       process.env['WS_HOST']       ?? '0.0.0.0',
  pskHex:       process.env['AC2_PSK'] ?? 'DEADBEEFCAFEBABE0123456789ABCDEF',
  heartbeatMs:  Number(process.env['HEARTBEAT_MS'] ?? '1000'),
  logLevel:     process.env['LOG_LEVEL'] ?? 'info',
} as const;
```

`process.env['KEY'] ?? 'default'` — reads environment variable KEY. If it is undefined (not set), uses the default value on the right of `??`.

`as const` — tells TypeScript that the shape and values of this object will never change. It locks the type to the exact literal values you wrote. Without it, TypeScript would infer `serialBaud: number` — with it, TypeScript knows the whole object structure is frozen.

**Key settings explained:**

| Setting | Default | Meaning |
|---------|---------|---------|
| `serialPort` | `/dev/ttyUSB0` | Path to the USB-serial device (STM32 Virtual COM Port shows up as `/dev/ttyACM0` on Linux). Override with `SERIAL_PORT=/dev/ttyACM0 ./dev.sh` |
| `serialBaud` | `115200` | UART baud rate — must match `edge/bsp/uart_driver.cpp` |
| `wsPort` | `8443` | TCP port the WebSocket server listens on |
| `wsHost` | `0.0.0.0` | Listen on all network interfaces. `0.0.0.0` means "any interface" (localhost + LAN). |
| `pskHex` | `DEADBEEF...` | 16-byte Pre-Shared Key as a hex string. **Must match `kPsk[]` in `edge/app/main.cpp`.** The edge and gateway use the same key or HMAC verification fails every frame. |
| `heartbeatMs` | `1000` | How often the gateway sends a heartbeat frame to the edge (milliseconds). |
| `logLevel` | `info` | Minimum log level to print. Set `LOG_LEVEL=debug` to see every frame. |

---

## log() function

```typescript
export type LogLevel = 'debug' | 'info' | 'warn' | 'error';

export function log(level: LogLevel, msg: string, data?: unknown): void {
  const levels: Record<LogLevel, number> = { debug: 0, info: 1, warn: 2, error: 3 };
  const configured = (levels[Config.logLevel as LogLevel] ?? 1);
  if (levels[level] < configured) return;
  ...
}
```

**How the level filter works:**

Each level gets a numeric rank: debug=0, info=1, warn=2, error=3. If you configured `logLevel: 'warn'` (rank=2) and a call arrives with level `'info'` (rank=1), then `1 < 2` is true so the function returns early without printing. Only messages with a rank ≥ configured rank are printed.

```typescript
const ts  = new Date().toISOString();
const line = data !== undefined
  ? `[${ts}] ${level.toUpperCase()} ${msg} ${JSON.stringify(data)}`
  : `[${ts}] ${level.toUpperCase()} ${msg}`;
```

`new Date().toISOString()` — returns a timestamp string like `"2026-04-30T14:23:05.123Z"`.

`JSON.stringify(data)` — converts the optional extra data object to a string so it can be printed. Useful for including things like `{ seq: 5, cmd: '0x21' }` next to the log message.

```typescript
if (level === 'error') {
  process.stderr.write(line + '\n');
} else {
  process.stdout.write(line + '\n');
}
```

`process.stderr` / `process.stdout` — Node.js streams for standard error and standard output. Errors go to stderr so they can be separated from normal output in shell piping. `dev.sh` captures stdout and prefixes it with `[gateway]`.

---

## Why a single global log function?

In small Node.js services, a single module-level function is the simplest approach. All files import `{ log }` from config.ts. No dependency injection needed. If you wanted structured logging (like JSON logs for a log aggregator), you would change this one function and all callers benefit.
