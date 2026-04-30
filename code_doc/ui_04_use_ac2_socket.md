# UI — hooks/useAC2Socket.ts

**File:** `ui/hooks/useAC2Socket.ts`

---

## What this file is

A custom React hook that manages the entire WebSocket connection lifecycle: connecting, parsing incoming messages, exposing state to the dashboard, and providing a `send()` function for outgoing commands.

A React "hook" is a function whose name starts with `use`. It can call other React hooks (`useState`, `useEffect`, etc.) and encapsulates stateful logic that components can reuse.

---

## Exported types

```typescript
export type ConnStatus = 'connecting' | 'connected' | 'disconnected';
```

A TypeScript union type — `ConnStatus` can only be one of these three strings. This is safer than a plain `string` because TypeScript will catch typos like `'conected'` at compile time.

```typescript
export interface Telemetry {
  state: number;
  cpu_load_x10: number;
  free_stack_min_words: number;
  hb_miss_count: number;
}
```

The shape of a `evt.telemetry` message payload. `cpu_load_x10` is an integer — CPU load × 10, so 153 means 15.3%. The dashboard divides by 10 for display: `(telemetry.cpu_load_x10 / 10).toFixed(1)`.

```typescript
export interface LogEntry {
  id: number;
  ts: number;
  type: string;
  text: string;
  level: 'info' | 'warn' | 'error' | 'ok';
}
```

One entry in the event log. `id` is a module-level counter (`logId++`) used as the React `key` prop (required for list rendering performance). `ts` is `Date.now()` when the event arrived.

---

## Module-level variables

```typescript
let logId  = 0;
let cmdSeq = 0;
```

These live outside the hook function — they persist across re-renders and re-connections. `logId` is a monotonically increasing counter for unique React keys. `cmdSeq` is the outgoing WebSocket command sequence number.

Why not `useRef`? Because they don't need to trigger re-renders when they change, and they don't need to be reset when the component unmounts. A module-level variable is the simplest solution.

---

## useAC2Socket(url)

```typescript
export function useAC2Socket(url: string) {
  const wsRef   = useRef<WebSocket | null>(null);
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const [status,    setStatus]    = useState<ConnStatus>('connecting');
  const [telemetry, setTelemetry] = useState<Telemetry | null>(null);
  const [sysInfo,   setSysInfo]   = useState<SystemInfo>({ uptime_ms: 0 });
  const [log,       setLog]       = useState<LogEntry[]>([]);
  ...
}
```

`useRef<WebSocket | null>(null)` — a ref that holds the WebSocket object. `useRef` stores a mutable value that does NOT trigger a re-render when changed. The WebSocket object itself is not display state — we don't want React to re-render every time the socket's internal state changes. We just need to be able to call `.send()` and `.close()` on it.

`useState<ConnStatus>('connecting')` — React state. When `setStatus(newValue)` is called, React schedules a re-render. The dashboard component re-renders and the `ConnDot` shows the new color.

`useState<Telemetry | null>(null)` — starts as `null` because no telemetry has arrived yet. The dashboard shows `—` when `telemetry === null`.

---

## addLog()

```typescript
const addLog = useCallback((type: string, text: string, level: LogEntry['level'] = 'info') => {
  setLog(prev => {
    const entry: LogEntry = { id: logId++, ts: Date.now(), type, text, level };
    return [entry, ...prev].slice(0, 30);
  });
}, []);
```

`useCallback(fn, deps)` — memoizes the function. Returns the same function object across re-renders as long as `deps` hasn't changed. `[]` means "never change". This prevents unnecessary re-renders in child components that receive `addLog` as a prop.

`setLog(prev => ...)` — the functional form of setState. Takes the previous value as an argument and returns the new value. This is required when the new state depends on the previous state (race condition avoidance — two rapid calls to `setLog` would both see the same stale `prev` if you used `setLog([entry, ...log].slice(0,30))`).

`[entry, ...prev].slice(0, 30)` — prepend the new entry to the front of the array. `...prev` spreads all existing entries. `.slice(0, 30)` keeps only the 30 most recent entries (oldest entries at the back are discarded).

---

## connect()

```typescript
const connect = useCallback(() => {
  if (wsRef.current?.readyState === WebSocket.OPEN) return;

  setStatus('connecting');
  const ws = new WebSocket(url, 'ac2.v2');
  wsRef.current = ws;

  ws.onopen = () => {
    setStatus('connected');
    addLog('SYS', 'WebSocket connected', 'ok');
  };

  ws.onclose = () => {
    setStatus('disconnected');
    addLog('SYS', 'WebSocket disconnected — retrying in 3s', 'warn');
    timerRef.current = setTimeout(connect, 3000);
  };

  ws.onerror = () => {
    addLog('SYS', 'WebSocket error', 'error');
  };

  ws.onmessage = (evt) => { ... };
}, [url, addLog]);
```

`wsRef.current?.readyState === WebSocket.OPEN` — the `?.` optional chaining: if `wsRef.current` is null, the expression short-circuits to `undefined` (not an error). Prevents calling `connect()` again if a connection is already open.

`new WebSocket(url, 'ac2.v2')` — creates a WebSocket connection. The second argument is the subprotocol. The gateway rejects connections without `'ac2.v2'`.

`ws.onopen`, `ws.onclose`, `ws.onerror`, `ws.onmessage` — direct property assignments for WebSocket event handlers. An alternative is `ws.addEventListener(...)`, but direct property assignment is simpler when you only need one handler per event.

`setTimeout(connect, 3000)` — schedules the `connect` function to run again in 3 seconds after a disconnect. This creates the automatic reconnect loop. The timer handle is stored in `timerRef` so it can be cancelled in cleanup.

---

## ws.onmessage — message parsing

```typescript
ws.onmessage = (evt) => {
  try {
    const envelope = JSON.parse(evt.data as string);
    const d = envelope.data ?? envelope;

    switch (d.type) {
      case 'evt.telemetry':
        setTelemetry({ state: d.state, cpu_load_x10: d.cpu_load_x10, ... });
        break;
      case 'evt.heartbeat':
        setSysInfo(prev => ({ ...prev, uptime_ms: d.uptime_ms }));
        break;
      case 'evt.report_state':
        addLog('STATE', `${STATE_NAMES[d.prev_state]} → ${STATE_NAMES[d.state]}`, 'info');
        break;
      case 'evt.ack':
        addLog('ACK', `seq ${d.echoed_seq}`, 'ok');
        break;
      case 'evt.nack':
        addLog('NACK', `seq ${d.echoed_seq} — ${ERR_CODES[d.err_code] ?? `ERR ${d.err_code}`}`, 'warn');
        break;
      ...
    }
  } catch { /* ignore malformed */ }
};
```

`evt.data as string` — type assertion. The WebSocket `MessageEvent.data` can be a string, ArrayBuffer, or Blob. We tell TypeScript to treat it as a string (which it always is since the gateway sends JSON text, not binary).

`envelope.data ?? envelope` — handles two message formats: the standard `{ v:2, ..., data: { type:..., ... } }` envelope, or a plain `{ type:..., ... }` object (if the gateway somehow sends unwrapped). The `??` falls back to the envelope itself if `.data` is null or undefined.

`STATE_NAMES[d.prev_state]` — looks up the human-readable state name. `STATE_NAMES = ['IDLE', 'SEARCH', 'TRACK', 'FAIL_SAFE']`. `d.prev_state = 1` → `'SEARCH'`.

`ERR_CODES[d.err_code] ?? 'ERR ${d.err_code}'` — translates numeric NACK error codes to strings. `ERR_CODES = { 1: 'INVALID_CMD', 2: 'INVALID_PAYLOAD', ... }`. If the code is unknown, falls back to `'ERR 9'` or similar.

`setSysInfo(prev => ({ ...prev, uptime_ms: d.uptime_ms }))` — spread operator creates a new object with all previous `sysInfo` fields, then overrides just `uptime_ms`. This preserves `version` and `git_sha` while updating `uptime_ms`.

---

## useEffect — mount and unmount

```typescript
useEffect(() => {
  connect();
  return () => {
    timerRef.current && clearTimeout(timerRef.current);
    wsRef.current?.close();
  };
}, [connect]);
```

`useEffect(setup, deps)` — runs `setup` after the component mounts. The returned function (`() => { clearTimeout...; ws.close(); }`) is the cleanup — it runs when the component unmounts (e.g. page navigation or test teardown).

`[connect]` dependency — runs this effect when `connect` changes. Since `connect` is wrapped in `useCallback([])`, it never changes, so this effect runs once on mount.

Cleanup: cancel the reconnect timer (`clearTimeout`) then close the socket (`ws.close()`). Without cleanup, a timer might fire after the component unmounts and try to call `setStatus` on an unmounted component (React would log a warning).

---

## send()

```typescript
const send = useCallback((cmd: OutCmd) => {
  if (wsRef.current?.readyState === WebSocket.OPEN) {
    const envelope = { v: 2, type: cmd.type, ts: Date.now(), seq: cmdSeq++, data: cmd };
    wsRef.current.send(JSON.stringify(envelope));
  }
}, []);
```

Builds the standard AC2 WebSocket envelope and sends it. `cmdSeq++` increments the global sequence counter — the gateway passes this seq to the edge, which checks for monotonically increasing values (replay protection).

Only sends if the socket is `OPEN`. If disconnected, the command is silently dropped. The UI disables buttons when `status !== 'connected'` so this case should not normally arise.

---

## Return value

```typescript
return { status, telemetry, sysInfo, log, send };
```

The hook exposes only what the dashboard needs:
- `status` — for the connection indicator
- `telemetry` — for the metric panel
- `sysInfo` — for uptime and version
- `log` — for the event log component
- `send` — for control buttons
