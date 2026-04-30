# Gateway — WsServer.ts

**File:** `gateway/src/ws/WsServer.ts`

---

## What this file is

`WsServer` wraps the `ws` npm library to provide a WebSocket server. It accepts browser clients, validates incoming messages, and provides a `broadcast()` method to send events to all connected clients.

Like `SerialBridge`, it extends `EventEmitter` and emits a `'command'` event when a valid command arrives. `Bridge` subscribes to this event.

---

## WebSocket subprotocol

```typescript
const SUBPROTOCOL = 'ac2.v2';
```

WebSocket connections carry a subprotocol string that the client and server negotiate. The browser sends `new WebSocket(url, 'ac2.v2')`. The server rejects clients that don't specify `'ac2.v2'`:

```typescript
if (ws.protocol !== SUBPROTOCOL) {
  ws.close(1002, 'subprotocol must be ac2.v2');
  return;
}
```

`ws.close(1002, reason)` — sends a WebSocket close frame with status code 1002 (Protocol Error) and a human-readable reason string. This is the correct way to reject a client at the WebSocket protocol level.

---

## Constructor

```typescript
constructor(host: string, port: number) {
  super();
  this.wss = new WebSocketServer({ host, port });

  this.wss.on('listening', () => {
    log('info', `WebSocket server listening`, { host, port });
  });

  this.wss.on('connection', (ws: WebSocket, req) => {
    const ip = req.socket.remoteAddress ?? 'unknown';
    log('info', 'WS client connected', { ip });

    if (ws.protocol !== SUBPROTOCOL) { ... ws.close(1002, ...); return; }

    ws.on('message', (raw) => { this.handleMessage(raw.toString(), ws); });
    ws.on('close', () => { log('info', 'WS client disconnected', { ip }); });
    ws.on('error', (err) => { log('error', 'WS client error', err.message); });
  });
}
```

`new WebSocketServer({ host, port })` — creates a TCP server listening on `host:port`. On Linux, `0.0.0.0` binds to all interfaces. On macOS, you may need `127.0.0.1` for localhost-only.

`this.wss.on('connection', (ws, req) => { ... })` — fires whenever a new client connects. `ws` is the WebSocket connection object for this specific client. `req` is the raw HTTP upgrade request (WebSocket starts as HTTP then upgrades).

`req.socket.remoteAddress` — the client's IP address. `?? 'unknown'` provides a fallback if it is undefined (possible in some edge cases).

For each new client, three event handlers are registered on `ws` (not on `this.wss`): `'message'`, `'close'`, `'error'`. These are per-connection handlers.

---

## broadcast()

```typescript
broadcast(event: OutboundEvent): void {
  const msg = JSON.stringify(envelope(this.outSeq++, event));
  for (const client of this.wss.clients) {
    if (client.readyState === WebSocket.OPEN) {
      client.send(msg);
    }
  }
}
```

`this.outSeq++` — post-increment. Each broadcast gets a unique sequence number so the browser can detect gaps or duplicate messages.

`JSON.stringify(envelope(...))` — converts the event object to a JSON string. The `envelope()` function wraps it in `{ v:2, type, ts, seq, data }`.

`this.wss.clients` — a `Set<WebSocket>` maintained by the `ws` library. All currently connected clients. We iterate over all of them and send to each one that is still open.

`client.readyState === WebSocket.OPEN` — check before sending. `readyState` can be `CONNECTING`, `OPEN`, `CLOSING`, or `CLOSED`. Sending to a non-OPEN socket would throw or silently fail.

`client.send(msg)` — sends the JSON string to the client. Fire-and-forget (no callback or await).

---

## handleMessage()

```typescript
private handleMessage(raw: string, _ws: WebSocket): void {
  let json: unknown;
  try {
    json = JSON.parse(raw);
  } catch {
    log('warn', 'WS: non-JSON message ignored');
    return;
  }

  const envResult = WsEnvelopeSchema.safeParse(json);
  if (!envResult.success) {
    log('warn', 'WS: invalid envelope', envResult.error.issues);
    return;
  }

  const cmdResult = InboundCmdSchema.safeParse(envResult.data.data);
  if (!cmdResult.success) {
    log('warn', 'WS: unrecognised command', cmdResult.error.issues);
    return;
  }

  this.emit('command', cmdResult.data, envResult.data.seq);
}
```

Two-layer validation:

1. **Envelope validation** — `WsEnvelopeSchema.safeParse(json)`. Checks the outer `{ v:2, type, ts, seq, data }` structure. `.safeParse()` returns `{ success: true, data: ... }` or `{ success: false, error: ... }` instead of throwing. This lets us handle invalid input gracefully without a try/catch.

2. **Command validation** — `InboundCmdSchema.safeParse(envResult.data.data)`. Validates the inner `data` field as one of the known command schemas. Zod's discriminated union automatically picks the right sub-schema based on the `type` field.

`this.emit('command', cmdResult.data, envResult.data.seq)` — emits the `'command'` event with the validated, typed command object and the envelope's seq number. `Bridge` has subscribed to this event and will process the command.

`_ws: WebSocket` — the underscore prefix is a TypeScript/ESLint convention meaning "this parameter is intentionally unused." The `ws` parameter is there in case we need to send a response back to the specific sender (e.g. for a request-response pattern), but currently all responses go to all clients via `broadcast()`.
