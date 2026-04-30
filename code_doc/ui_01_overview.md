# UI — Architecture Overview

**Directory:** `ui/`

---

## What the UI is

The Command & Control Interface (CCI) is a Next.js 14 web application. It connects to the gateway over WebSocket and displays live telemetry from the STM32 edge node, while allowing the operator to send state-change commands.

```
Browser (Next.js)
│
├── useAC2Socket (hook) ──── WebSocket ──── Gateway (ws://localhost:8443)
│
├── app/page.tsx ─────────── Dashboard layout + control logic
├── components/RadarDisplay — Canvas animation (state-driven)
└── components/EventLog ──── Scrolling event history
```

---

## File map

| File | Role |
|------|------|
| `app/layout.tsx` | Root HTML structure, metadata, global CSS |
| `app/page.tsx` | Main dashboard — telemetry display, state controls, layout |
| `hooks/useAC2Socket.ts` | WebSocket hook — connection, message parsing, reconnect |
| `components/RadarDisplay.tsx` | Animated canvas radar (sweep, blips, state-driven) |
| `components/EventLog.tsx` | Scrolling log of events from the gateway |

---

## Technology choices

**Next.js 14 App Router** — The `app/` directory uses Next.js's App Router. The dashboard page is a Client Component (`'use client'`) because it uses React hooks (`useState`, `useEffect`) and browser APIs (WebSocket, Canvas). Server Components cannot use these.

**No external UI library** — All styling is done with CSS Modules (`.module.css` files). Each component has its own scoped CSS file.

**WebSocket, not HTTP polling** — The gateway pushes events as they happen (state changes, telemetry ticks, fault reports). HTTP polling would add latency and unnecessary load.

---

## State machine awareness in the UI

The UI enforces state machine transition rules locally (in addition to the edge enforcing them):

```typescript
// TRACK is only reachable from SEARCH, not from IDLE
disabled={!isSafe || state===2 || state===0}
```

- The TRACK button is disabled when `state===0` (IDLE). You cannot jump directly to TRACK.
- This mirrors the edge firmware rule: IDLE → SEARCH → TRACK.
- A NACK with error `INVALID_TRANSITION` (code 3) would appear in the event log if somehow this rule is bypassed.

---

## Connection lifecycle

The `useAC2Socket` hook manages the WebSocket connection:

1. On mount → connects to `ws://localhost:8443` with subprotocol `'ac2.v2'`
2. On connect → status → `'connected'`, logs "WebSocket connected"
3. On message → parses envelope, dispatches to correct state update
4. On close → status → `'disconnected'`, schedules reconnect in 3 seconds
5. On unmount → cancels reconnect timer, closes socket

---

## Reading order

1. [ui_02_layout.md](ui_02_layout.md) — root structure
2. [ui_04_use_ac2_socket.md](ui_04_use_ac2_socket.md) — WebSocket hook (core data layer)
3. [ui_03_page.md](ui_03_page.md) — main dashboard (uses the hook)
4. [ui_05_radar_display.md](ui_05_radar_display.md) — canvas animation component
5. [ui_06_event_log.md](ui_06_event_log.md) — event log component
