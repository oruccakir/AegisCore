# UI — components/EventLog.tsx

**File:** `ui/components/EventLog.tsx`

---

## What this file is

A pure display component — it receives the `entries` array from `useAC2Socket` and renders them as a scrolling list with timestamps, type labels, and color-coded severity.

"Pure display" means it has no state and no side effects. Given the same props, it always renders the same output. This makes it simple to reason about and test.

---

## Full source

```typescript
'use client';
import type { LogEntry } from '@/hooks/useAC2Socket';
import styles from './EventLog.module.css';

interface Props { entries: LogEntry[]; }

const LEVEL_COLOR: Record<string, string> = {
  ok:    '#00ff41',
  info:  '#4a8a54',
  warn:  '#ffaa00',
  error: '#ff2222',
};

function fmt(ts: number) {
  const d = new Date(ts);
  return `${String(d.getHours()).padStart(2,'0')}:${String(d.getMinutes()).padStart(2,'0')}:${String(d.getSeconds()).padStart(2,'0')}.${String(d.getMilliseconds()).padStart(3,'0')}`;
}

export default function EventLog({ entries }: Props) {
  return (
    <div className={styles.wrap}>
      <div className={styles.header}>EVENT LOG</div>
      <div className={styles.list}>
        {entries.length === 0 && (
          <div className={styles.empty}>— awaiting events —</div>
        )}
        {entries.map((e, i) => (
          <div key={e.id} className={styles.entry} style={{ animationDelay: i === 0 ? '0ms' : undefined }}>
            <span className={styles.time}>{fmt(e.ts)}</span>
            <span className={styles.type} style={{ color: LEVEL_COLOR[e.level] }}>{e.type.padEnd(6)}</span>
            <span className={styles.text}>{e.text}</span>
          </div>
        ))}
      </div>
    </div>
  );
}
```

---

## Line by line

```typescript
'use client';
```

Client Component directive. Required because this component might be used in a Next.js context where the default is Server Components. Even though `EventLog` itself has no hooks, its parent (`page.tsx`) is a Client Component and passes down props that React renders — marking it `'use client'` ensures it is bundled for the browser.

```typescript
import type { LogEntry } from '@/hooks/useAC2Socket';
```

`import type` — imports only the TypeScript type definition, no runtime code. `@/` is a Next.js path alias for the project root (`ui/`). So this resolves to `ui/hooks/useAC2Socket.ts`.

```typescript
const LEVEL_COLOR: Record<string, string> = {
  ok:    '#00ff41',
  info:  '#4a8a54',
  warn:  '#ffaa00',
  error: '#ff2222',
};
```

Maps the four log levels to hex color strings. `ok` is bright green (success), `info` is dim green, `warn` is amber, `error` is red. These match the overall radar color palette.

`Record<string, string>` — a TypeScript generic type. `Record<K, V>` means "an object whose keys are type K and values are type V." Equivalent to `{ [key: string]: string }`.

---

## fmt() — timestamp formatter

```typescript
function fmt(ts: number) {
  const d = new Date(ts);
  return `${String(d.getHours()).padStart(2,'0')}:...:${String(d.getMilliseconds()).padStart(3,'0')}`;
}
```

`new Date(ts)` — creates a Date object from a Unix millisecond timestamp. The timestamp came from `Date.now()` when the log entry was created.

`d.getHours()` — returns 0–23. Not using `getUTCHours()` — local timezone is correct for an operator display.

`padStart(3,'0')` — milliseconds need 3 digits: `7` → `'007'`, `42` → `'042'`, `123` → `'123'`.

Result format: `14:23:05.123` — hours:minutes:seconds.milliseconds.

---

## Empty state

```typescript
{entries.length === 0 && (
  <div className={styles.empty}>— awaiting events —</div>
)}
```

Shows a placeholder message when no events have arrived yet (before the gateway sends anything). React's `{condition && <element>}` pattern renders nothing if condition is false.

---

## Entries list

```typescript
{entries.map((e, i) => (
  <div key={e.id} className={styles.entry} style={{ animationDelay: i === 0 ? '0ms' : undefined }}>
    <span className={styles.time}>{fmt(e.ts)}</span>
    <span className={styles.type} style={{ color: LEVEL_COLOR[e.level] }}>{e.type.padEnd(6)}</span>
    <span className={styles.text}>{e.text}</span>
  </div>
))}
```

`entries.map((e, i) => ...)` — transforms the array of `LogEntry` objects into an array of JSX elements. React renders this array as sibling DOM nodes.

`key={e.id}` — required by React for list rendering. React uses the key to match DOM nodes to array items when the list changes. Without a key (or with `key={i}`), React would re-render every item in the list when a new item is prepended. With `key={e.id}` (unique and stable), React only creates a new DOM node for the new entry.

`animationDelay: i === 0 ? '0ms' : undefined` — the CSS class `styles.entry` has a `slide-in` animation. Only the newest entry (index 0, because the array is newest-first) gets `animationDelay: '0ms'`, which means it plays immediately. Other entries do not override the default, so they have no delay animation on subsequent renders (they are already in the DOM). This creates the visual effect of new entries sliding in at the top.

`e.type.padEnd(6)` — right-pads the type string to 6 characters. `'ACK'` becomes `'ACK   '`, `'STATE'` becomes `'STATE '`. This aligns the text column, making it easier to scan visually. Works because the CSS uses a monospace font (`Share Tech Mono`).

`LEVEL_COLOR[e.level]` — dynamic color lookup. The `type` label (ACK, NACK, STATE, etc.) is colored according to the entry's severity level.

---

## Why no state in this component?

All state lives in `useAC2Socket`. `EventLog` just renders what it receives. This separation of concerns means:
- `EventLog` can be tested by passing any `entries` array
- The log scrolling, filtering, or truncation logic lives in the hook, not scattered in the component
- The component is a pure function of its props — easier to reason about
