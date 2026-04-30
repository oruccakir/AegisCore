# Gateway — ws/schemas.ts

**File:** `gateway/src/ws/schemas.ts`

---

## What this file is

All WebSocket message types are defined here — both the runtime validation schemas (using the Zod library) and the TypeScript type definitions. Every message that crosses the WebSocket connection is defined in this one file.

---

## Zod — what and why

Zod is a TypeScript-first schema validation library. You describe the shape of data once, and Zod:
1. Validates incoming JSON at runtime (rejects unknown fields, wrong types, missing fields).
2. Infers TypeScript types automatically — so you do not write the same shape twice (once as a TypeScript interface and once as a validator).

Without Zod, you would write:
```typescript
interface Cmd { type: string; targetState: string; }
// and separately:
if (typeof data.type !== 'string') reject();
if (typeof data.targetState !== 'string') reject();
```

With Zod:
```typescript
const schema = z.object({ type: z.literal('cmd.set_state'), targetState: z.enum(['idle', ...]) });
type Cmd = z.infer<typeof schema>; // type is automatically derived
```

---

## WebSocket envelope

```typescript
export const WsEnvelopeSchema = z.object({
  v:    z.literal(2),
  type: z.string(),
  ts:   z.number(),
  seq:  z.number().int().nonnegative(),
  data: z.unknown(),
});
export type WsEnvelope = z.infer<typeof WsEnvelopeSchema>;
```

Every WebSocket message (both directions) is wrapped in this envelope. Fields:

- `v: 2` — protocol version. `z.literal(2)` means it must be exactly the number 2. Any other value fails validation.
- `type: string` — message type string, e.g. `'cmd.set_state'` or `'evt.telemetry'`.
- `ts: number` — Unix timestamp in milliseconds (`Date.now()`).
- `seq: number` — message sequence number. `.int()` means no decimal point. `.nonnegative()` means ≥ 0.
- `data: unknown` — the actual message payload. `z.unknown()` allows any type — the specific shape is validated separately by the `InboundCmdSchema`.

`z.infer<typeof WsEnvelopeSchema>` — TypeScript type inference. `typeof WsEnvelopeSchema` gets the type of the Zod schema object. `z.infer<...>` extracts the TypeScript type that the schema represents. Result: `WsEnvelope` has exactly the shape described above.

---

## Inbound command schemas (browser → gateway)

```typescript
export const CmdSetStateSchema = z.object({
  type:       z.literal('cmd.set_state'),
  targetState: z.enum(['idle', 'search', 'track', 'fail_safe']),
});
```

`z.literal('cmd.set_state')` — the field must be the exact string `'cmd.set_state'`. This is how Zod discriminates between command types.

`z.enum(['idle', 'search', 'track', 'fail_safe'])` — the value must be one of these four strings. Anything else (e.g. `'IDLE'`, `'armed'`) is rejected.

```typescript
export const InboundCmdSchema = z.discriminatedUnion('type', [
  CmdSetStateSchema,
  CmdManualLockSchema,
  CmdGetVersionSchema,
  CmdHeartbeatSchema,
]);
export type InboundCmd = z.infer<typeof InboundCmdSchema>;
```

`z.discriminatedUnion('type', [...])` — validates the object against whichever sub-schema matches the `type` field. Zod looks at `data.type`, finds `'cmd.set_state'`, and validates against `CmdSetStateSchema`. This is more efficient than trying each schema in order.

`InboundCmd` (the TypeScript type) becomes `CmdSetState | CmdManualLock | CmdGetVersion | CmdHeartbeat` — a discriminated union type. TypeScript narrows this automatically in switch statements.

---

## Outbound event interfaces (gateway → browser)

These are plain TypeScript `interface` definitions (no Zod schemas) because the gateway *creates* these objects — it doesn't need to validate them at runtime. TypeScript's compile-time checks are sufficient.

```typescript
export interface EvtTelemetry {
  type:               'evt.telemetry';
  state:              number;
  cpu_load_x10:       number;
  free_stack_min_words: number;
  hb_miss_count:      number;
}
```

`type: 'evt.telemetry'` — a literal type in the TypeScript interface. When TypeScript sees `x.type`, it knows it must be exactly `'evt.telemetry'`.

```typescript
export type OutboundEvent =
  | EvtReportState
  | EvtTelemetry
  | EvtFaultReport
  | EvtHeartbeat
  | EvtVersionReport
  | EvtNack
  | EvtAck
  | EvtError;
```

Union type — `OutboundEvent` can be any of these interfaces. Functions that accept `OutboundEvent` can handle all message types. TypeScript discriminates based on the `type` field.

---

## envelope()

```typescript
export function envelope(seq: number, event: OutboundEvent): WsEnvelope {
  return { v: 2, type: event.type, ts: Date.now(), seq, data: event };
}
```

Helper that wraps an event in the standard envelope. Called by `WsServer.broadcast()`. Produces the JSON structure the browser expects: `{ v:2, type:'evt.telemetry', ts:1234567890123, seq:5, data:{ type:'evt.telemetry', state:1, ... } }`.

Note that `type` appears both in the envelope and in `data`. The envelope `type` is for routing; `data.type` is for the event itself.
