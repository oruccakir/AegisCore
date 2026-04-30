# UI — components/RadarDisplay.tsx

**File:** `ui/components/RadarDisplay.tsx`

---

## What this file is

An animated canvas-based radar display. It receives the current system state as a prop and renders an appropriate animation:

- **IDLE / FAIL_SAFE (state 0, 3)** — static display showing "STANDBY"
- **SEARCH (state 1)** — rotating sweep line with green trail
- **TRACK (state 2)** — faster sweep plus simulated target blips

Everything is drawn using the HTML5 Canvas 2D API via `requestAnimationFrame`.

---

## Props and refs

```typescript
interface Props { state: number; }

const TRAIL_COUNT = 60;

export default function RadarDisplay({ state }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const angleRef  = useRef(0);
  const trailsRef = useRef<number[]>([]);
  const rafRef    = useRef<number>(0);
  ...
}
```

`useRef<HTMLCanvasElement>(null)` — a ref to the `<canvas>` DOM element. Refs give direct access to DOM nodes without re-rendering. We need the canvas element to call `.getContext('2d')`.

`angleRef = useRef(0)` — stores the current sweep angle in radians. Updated on every animation frame. Using a ref (not state) because changing the angle should not trigger a React re-render — the canvas draws itself. Using `setState` for the angle would cause React to re-run the component on every frame, which is wasteful.

`trailsRef = useRef<number[]>([])` — array of angles where simulated blip echoes were detected (TRACK mode). Persistent across frames. Cleared implicitly when `state` changes (the effect restarts).

`rafRef = useRef<number>(0)` — stores the `requestAnimationFrame` handle returned by the browser. Needed to cancel the animation when the component is about to re-run the effect (state changed) or unmount.

---

## useEffect

```typescript
useEffect(() => {
  const canvas = canvasRef.current;
  if (!canvas) return;
  const ctx = canvas.getContext('2d')!;

  const SIZE = 300;
  canvas.width  = SIZE;
  canvas.height = SIZE;
  const cx = SIZE / 2;
  const cy = SIZE / 2;
  const R  = SIZE / 2 - 8;

  const isActive = state === 1 || state === 2;
  const speed    = state === 2 ? 0.04 : 0.025;
  ...
  loop();
  return () => cancelAnimationFrame(rafRef.current);
}, [state]);
```

`[state]` dependency — the effect re-runs whenever `state` changes. This restarts the animation with the new `isActive` and `speed` values.

`canvas.width = SIZE; canvas.height = SIZE` — sets the canvas resolution in pixels. Important: CSS can scale a canvas element, but the drawing resolution is determined by these properties. Setting them resets (clears) the canvas.

`const R = SIZE / 2 - 8` — the radar circle radius. 8px margin from the edge for the outer ring.

`const speed = state === 2 ? 0.04 : 0.025` — TRACK mode sweeps faster (0.04 radians/frame ≈ 2.3°/frame) than SEARCH mode (0.025 rad/frame ≈ 1.4°/frame).

`return () => cancelAnimationFrame(rafRef.current)` — cleanup. `cancelAnimationFrame` stops the animation loop when the effect is about to re-run (state changed) or when the component unmounts. Without this, multiple loops would run simultaneously after state changes.

---

## draw() — frame render

```typescript
function draw() {
  ctx.clearRect(0, 0, SIZE, SIZE);

  // Background
  ctx.fillStyle = '#020a03';
  ctx.beginPath();
  ctx.arc(cx, cy, R + 4, 0, Math.PI * 2);
  ctx.fill();
  ...
}
```

`ctx.clearRect(0, 0, SIZE, SIZE)` — clears the entire canvas to transparent black. Called at the start of every frame to remove the previous frame's drawing.

`ctx.arc(cx, cy, R + 4, 0, Math.PI * 2)` — draws a full circle (0 to 2π radians). The circle is `R + 4` pixels — slightly larger than the radar circle to create a dark background for it.

---

## Range rings and crosshairs

```typescript
for (let i = 1; i <= 4; i++) {
  ctx.beginPath();
  ctx.arc(cx, cy, (R * i) / 4, 0, Math.PI * 2);
  ctx.strokeStyle = i === 4 ? '#0a3015' : '#061a0b';
  ctx.lineWidth = i === 4 ? 1.5 : 0.8;
  ctx.stroke();
}
```

Four concentric circles at 25%, 50%, 75%, 100% of the radar radius. The outermost ring (i=4) is slightly brighter (`#0a3015`) and thicker (1.5px) to define the radar boundary. Inner rings are very dim `#061a0b` (barely visible green-black).

`ctx.stroke()` — actually draws the path as an outline. `ctx.fill()` would fill it solid.

---

## Sweep trail

```typescript
const trailLen = Math.PI * 1.1;
for (let i = 0; i < TRAIL_COUNT; i++) {
  const frac    = i / TRAIL_COUNT;
  const trailA  = angleRef.current - frac * trailLen;
  const alpha   = (1 - frac) * 0.55;
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.arc(cx, cy, R, trailA, trailA + trailLen / TRAIL_COUNT);
  ctx.closePath();
  ctx.fillStyle = `rgba(0, 255, 65, ${alpha * 0.12})`;
  ctx.fill();
}
```

`trailLen = Math.PI * 1.1` — the trail spans about 200° (1.1 × 180°) behind the sweep line.

The loop draws 60 thin pie-slice segments. Each segment:
- `frac = i / 60` — fraction from 0 (at sweep tip) to 1 (trail end)
- `trailA` — starting angle of this segment, going backward from current angle
- `alpha = (1 - frac) * 0.55` — opacity: brightest at the sweep tip (frac=0 → alpha=0.55), fading to 0 at the trail end

The result is a glowing phosphor-screen decay effect.

---

## Sweep line

```typescript
const sweep = angleRef.current;
const grd = ctx.createLinearGradient(cx, cy,
  cx + Math.cos(sweep) * R,
  cy + Math.sin(sweep) * R);
grd.addColorStop(0, 'rgba(0,255,65,0)');
grd.addColorStop(1, 'rgba(0,255,65,0.9)');
ctx.beginPath();
ctx.moveTo(cx, cy);
ctx.lineTo(cx + Math.cos(sweep) * R, cy + Math.sin(sweep) * R);
ctx.strokeStyle = grd;
ctx.lineWidth = 2;
ctx.stroke();
```

`ctx.createLinearGradient(x0, y0, x1, y1)` — creates a gradient from point (x0,y0) to (x1,y1). Here from center (cx,cy) to the tip of the sweep line.

`Math.cos(sweep) * R` — x coordinate of the sweep tip. In standard math, angle 0 points right (+x), π/2 points down (+y) (because canvas y-axis points down). The sweep rotates clockwise on screen as `angleRef.current` increases.

`grd.addColorStop(0, ...)` — at the center, the line is transparent (alpha=0). At the tip (stop 1), it is 90% opaque bright green. This gives the line a fading-from-center appearance.

---

## Blips (TRACK mode)

```typescript
if (state === 2) {
  for (const a of trailsRef.current) {
    const diff = ((angleRef.current - a) % (Math.PI * 2) + Math.PI * 2) % (Math.PI * 2);
    if (diff < Math.PI * 0.3) {
      const age   = diff / (Math.PI * 0.3);
      const dist  = R * 0.55;
      const bx    = cx + Math.cos(a) * dist;
      const by    = cy + Math.sin(a) * dist;
      ctx.beginPath();
      ctx.arc(bx, by, 3, 0, Math.PI * 2);
      ctx.fillStyle = `rgba(0,255,65,${1 - age})`;
      ctx.fill();
      ...
    }
  }
}
```

`trailsRef.current` — array of angles (radians) where "targets" were detected. New blip angles are added when the sweep completes a full rotation and the array has fewer than 5 blips.

`((angleRef.current - a) % (Math.PI * 2) + Math.PI * 2) % (Math.PI * 2)` — computes how far behind the current sweep angle the blip is. The double-modulo pattern handles the case where the difference crosses the 0/2π boundary (e.g. sweep is at 0.1, blip is at 6.1 — difference should be ~0.26, not ~-6.0).

`if (diff < Math.PI * 0.3)` — only show blips that are within ~54° behind the current sweep angle. Outside this window, the blip has "faded."

`age = diff / (Math.PI * 0.3)` — 0 at the sweep tip (just illuminated), 1 at the fading edge. Used to set alpha: `1 - age` → 1.0 (bright) to 0.0 (gone).

---

## loop() — animation loop

```typescript
function loop() {
  draw();
  rafRef.current = requestAnimationFrame(loop);
}
loop();
```

`requestAnimationFrame(loop)` — tells the browser to call `loop` before the next screen repaint. The browser calls this approximately 60 times per second (on a 60 Hz display). This is the standard web animation pattern — better than `setInterval` because `requestAnimationFrame` pauses when the tab is not visible, saving CPU.

The handle returned by `requestAnimationFrame` is stored in `rafRef.current`. This is what `cancelAnimationFrame(rafRef.current)` uses in cleanup to stop the loop.

`angleRef.current += speed` (inside `draw()`) — advances the sweep angle by `speed` radians per frame.
