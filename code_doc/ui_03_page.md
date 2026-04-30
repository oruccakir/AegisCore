# UI — app/page.tsx (Dashboard)

**File:** `ui/app/page.tsx`

---

## What this file is

The main dashboard component. It:
- Connects to the WebSocket via `useAC2Socket`
- Renders the three-column layout (radar | telemetry+controls | event log)
- Handles the state machine transition rules for button enablement
- Contains two small helper components: `Metric` and `StateBtn`

---

## Module-level constants

```typescript
const STATE_NAMES  = ['IDLE', 'SEARCH', 'TRACK', 'FAIL_SAFE'];
const STATE_COLORS = ['#4a8a54', '#00ff41', '#ffaa00', '#ff2222'];
const STATE_DESC   = [
  'System standing by. No active scan.',
  'Scanning sector. Awaiting target acquisition.',
  'Target acquired. Tracking in progress.',
  'FAIL-SAFE ENGAGED. System locked.',
];
```

Arrays indexed by state number (0–3). `STATE_COLORS[1]` → `'#00ff41'` (bright green for SEARCH). This pattern allows `STATE_COLORS[state]` to get the right color without a switch statement.

---

## fmtUptime()

```typescript
function fmtUptime(ms: number) {
  const s = Math.floor(ms / 1000);
  const m = Math.floor(s / 60);
  const h = Math.floor(m / 60);
  return `${String(h).padStart(2,'0')}:${String(m%60).padStart(2,'0')}:${String(s%60).padStart(2,'0')}`;
}
```

Converts milliseconds to `HH:MM:SS` format.

`Math.floor(ms / 1000)` — convert ms to whole seconds, discarding fractional part.

`m % 60` — modulo to get minutes within the current hour. Without `% 60`, if 65 minutes have elapsed, `m = 65` and you'd display `01:65:00` instead of `01:05:00`.

`String(h).padStart(2,'0')` — converts number to string and left-pads with `'0'` to ensure at least 2 characters. `String(5).padStart(2,'0')` → `'05'`.

---

## ConnDot component

```typescript
function ConnDot({ status }: { status: string }) {
  const color = status === 'connected' ? '#00ff41'
              : status === 'connecting' ? '#ffaa00'
              : '#ff2222';
  return (
    <span style={{ display:'inline-block', width:8, height:8, borderRadius:'50%',
      background: color, boxShadow: `0 0 6px ${color}`,
      animation: status === 'connecting' ? 'blink 1s infinite' : undefined }} />
  );
}
```

A small colored dot that reflects connection status. Three states:

- Connected → green (`#00ff41`)
- Connecting → amber (`#ffaa00`) with blinking animation
- Disconnected → red (`#ff2222`)

`borderRadius: '50%'` — makes a square element into a circle.

`boxShadow: '0 0 6px ${color}'` — adds a glow effect. The color of the glow matches the dot, creating a neon-light radar aesthetic.

`animation: 'blink 1s infinite'` — references the `@keyframes blink` CSS animation defined in `globals.css`. Only applied when `status === 'connecting'`; `undefined` means no animation.

---

## Dashboard component

```typescript
export default function Dashboard() {
  const { status, telemetry, sysInfo, log, send } = useAC2Socket('ws://localhost:8443');

  const state     = telemetry?.state ?? 0;
  const stateColor = STATE_COLORS[state] ?? '#4a8a54';
  const isFailSafe = state === 3;
  const isSafe     = !isFailSafe && status === 'connected';
  ...
}
```

`telemetry?.state ?? 0` — optional chaining + nullish coalescing. If `telemetry` is null (no message received yet), `?.state` is `undefined`, and `?? 0` provides the default state IDLE.

`isSafe` — combined check: system is not in fail-safe AND gateway is connected. Used to disable all control buttons when either condition is not met.

---

## Top bar

```typescript
<div className={styles.stateChip} style={{ borderColor: stateColor, color: stateColor,
  boxShadow: `0 0 10px ${stateColor}33` }}>
  <span className={styles.stateDot} style={{ background: stateColor, boxShadow: `0 0 6px ${stateColor}` }} />
  {STATE_NAMES[state]}
</div>
```

`className={styles.stateChip}` — CSS Module class. `styles` is imported from `page.module.css`. The actual CSS class name is generated as `page_stateChip__xxxxx` to be globally unique, preventing style collisions.

`style={{ borderColor: stateColor }}` — inline style overrides. The border color changes dynamically with state. CSS Modules handle the static parts (padding, font, etc.); inline styles handle the dynamic color.

`${stateColor}33` — appends `33` (hex for 20% opacity) to the color string. `#00ff4133` is bright green at 20% opacity — a subtle glow.

---

## Telemetry metrics panel

```typescript
<div className={styles.metrics}>
  <Metric label="UPTIME"      value={fmtUptime(sysInfo.uptime_ms)} />
  <Metric label="CPU LOAD"    value={telemetry ? `${(telemetry.cpu_load_x10 / 10).toFixed(1)}%` : '—'} />
  <Metric label="STACK FREE"  value={telemetry ? `${telemetry.free_stack_min_words}w` : '—'} />
  <Metric label="HB MISS"
    value={telemetry ? String(telemetry.hb_miss_count) : '—'}
    warn={(telemetry?.hb_miss_count ?? 0) > 0} />
</div>
```

`telemetry ? ... : '—'` — ternary: if telemetry is non-null, show the real value; otherwise show a dash placeholder.

`(telemetry.cpu_load_x10 / 10).toFixed(1)` — divides by 10 to get the actual percentage, then formats to one decimal place. `153 / 10 = 15.3`.

`warn={(telemetry?.hb_miss_count ?? 0) > 0}` — the `warn` prop makes the value amber-colored when heartbeat misses are > 0. A non-zero HB miss count means the gateway is not receiving heartbeats from the edge, which may indicate an impending fail-safe.

---

## State control buttons

```typescript
<StateBtn label="TRACK"  active={state===2}
  disabled={!isSafe || state===2 || state===0}
  color="#ffaa00"
  onClick={() => setSystemState('track')} />
```

`disabled={!isSafe || state===2 || state===0}`:
- `!isSafe` — disabled when fail-safe is active or disconnected
- `state===2` — disabled when already in TRACK (no point re-sending)
- `state===0` — disabled when in IDLE (TRACK requires going through SEARCH first)

This enforces the valid transitions: IDLE→SEARCH, SEARCH→TRACK, SEARCH→IDLE, TRACK→IDLE.

---

## Fail-safe and disconnect banners

```typescript
{isFailSafe && (
  <div className={styles.failBanner}>
    ⚠ FAIL-SAFE ENGAGED — SYSTEM LOCKED
  </div>
)}
{status === 'disconnected' && (
  <div className={styles.warnBanner}>
    ● GATEWAY DISCONNECTED — RECONNECTING…
  </div>
)}
```

Conditional rendering with `&&`. In React JSX, `{condition && <element>}` renders the element only if condition is truthy. If `isFailSafe` is false, the `<div>` is not rendered at all (no empty DOM node).

---

## Metric component

```typescript
function Metric({ label, value, warn }: { label: string; value: string; warn?: boolean }) {
  return (
    <div className={styles.metric}>
      <span className={styles.metricLabel}>{label}</span>
      <span className={styles.metricValue} style={{ color: warn ? 'var(--amber)' : undefined }}>
        {value}
      </span>
    </div>
  );
}
```

`warn?: boolean` — the `?` makes it optional. If not provided, it is `undefined`, which is falsy.

`color: warn ? 'var(--amber)' : undefined` — when `warn` is true, sets the text color to the amber CSS variable. When `undefined`, the browser uses the default color from the CSS class. Setting a style to `undefined` in React means "remove this inline style override, let CSS decide."

---

## StateBtn component

```typescript
function StateBtn({ label, active, disabled, color, onClick }:
  { label: string; active: boolean; disabled: boolean; color: string; onClick: () => void }) {
  return (
    <button
      className={`${styles.stateBtn} ${active ? styles.stateBtnActive : ''}`}
      style={active ? { borderColor: color, color, boxShadow: `0 0 12px ${color}44` } : undefined}
      disabled={disabled}
      onClick={onClick}>
      {active && <span className={styles.activeDot} style={{ background: color }} />}
      {label}
    </button>
  );
}
```

`` `${styles.stateBtn} ${active ? styles.stateBtnActive : ''}` `` — template literal that conditionally adds the `stateBtnActive` CSS class. This adds the active visual state (brighter border, different background) when this state button corresponds to the current system state.

`disabled={disabled}` — the HTML `disabled` attribute. A disabled button cannot be clicked and is styled grey by the browser. Our CSS also applies custom disabled styling.

`{active && <span .../>}` — shows a colored indicator dot inside the button when active.
