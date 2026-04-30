# UI — app/layout.tsx (Root Layout)

**File:** `ui/app/layout.tsx`

---

## What this file is

The root layout is the outermost wrapper for every page in the Next.js app. It renders once and wraps all pages with the `<html>` and `<body>` tags. It is a Server Component (no `'use client'` directive).

---

## Full source

```typescript
import type { Metadata } from 'next';
import './globals.css';

export const metadata: Metadata = {
  title: 'AegisCore CCI',
  description: 'Command & Control Interface',
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
```

---

## Line by line

```typescript
import type { Metadata } from 'next';
```

`import type` — imports only the TypeScript type, not any runtime code. `Metadata` is the type for the `metadata` export that Next.js reads to set `<title>` and `<meta>` tags.

```typescript
import './globals.css';
```

Imports the global CSS file. In Next.js, CSS imported in the root layout applies to the entire application. `globals.css` contains the CSS custom properties (variables) for the color theme (`--text-dim`, `--amber`, etc.) and the `@keyframes blink` animation. Every component can reference these variables.

```typescript
export const metadata: Metadata = {
  title: 'AegisCore CCI',
  description: 'Command & Control Interface',
};
```

Next.js reads this exported constant and uses it to populate `<title>AegisCore CCI</title>` and `<meta name="description" ...>` in the HTML `<head>`. This is the Next.js App Router way to set page metadata — no manual `<Head>` component needed.

```typescript
export default function RootLayout({ children }: { children: React.ReactNode }) {
```

`children: React.ReactNode` — TypeScript type for anything React can render (JSX elements, strings, numbers, arrays, null, etc.). `children` is passed by Next.js and is the current page component being rendered (in our case, `page.tsx`).

```typescript
return (
  <html lang="en">
    <body>{children}</body>
  </html>
);
```

`lang="en"` — HTML accessibility attribute. Screen readers and browsers use this to determine text-to-speech language.

`{children}` — inserts the current page's rendered output. When the browser navigates to `/`, Next.js renders `page.tsx` and passes it here as `children`.

---

## Why Server Component?

Layout does no interactivity — no state, no event handlers, no browser APIs. Next.js renders it on the server (or at build time), sends the HTML to the browser, and hydrates only the parts that need JavaScript. This reduces the initial JavaScript bundle.

`page.tsx` has `'use client'` because it uses React hooks. The boundary between Server and Client components is clear: layout stays server-side, page becomes client-side.
