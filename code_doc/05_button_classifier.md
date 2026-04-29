# ButtonClassifier — Short and Long Press Detection

**Files:** `edge/app/button_classifier.hpp`, `edge/app/button_classifier.cpp`

---

## What this module is

The physical button on the STM32F407G-DISC1 (User button, PA0) produces electrical noise when pressed or released. Without filtering, one press can generate dozens of spurious signals. `ButtonClassifier` solves two problems:

1. **Debouncing** — ignore edges that arrive within 20 ms of the previous edge (noise)
2. **Classification** — decide whether a completed press was "short" (< 500 ms) or "long" (≥ 500 ms)

The input is a stream of `RawButtonEdge` events from the ISR. The output is an `std::optional<Event>` — either nothing (debounce suppressed it, or waiting for release), or a `ButtonShortPress` / `ButtonLongPress` event.

---

## State variables

```cpp
bool has_last_edge_ = false;         // whether we've ever seen an edge
bool is_pressed_ = false;            // whether the button is currently held down
std::uint32_t last_edge_timestamp_ms_ = 0U;  // timestamp of most recent accepted edge
std::uint32_t press_started_ms_ = 0U;        // timestamp when press began
```

---

## Constants

```cpp
static constexpr std::uint32_t kDebounceMs = 20U;
```

Any edge that arrives within 20 ms of the previous one is treated as bounce and discarded.

```cpp
static constexpr std::uint32_t kLongPressThresholdMs = 500U;
```

A press held for 500 ms or longer is classified as `ButtonLongPress`.

---

## `OnEdge(const RawButtonEdge& edge)` → `std::optional<Event>`

This is the only public function. It is called by `StateMachineTask` every time a `RawButtonEdge` arrives from the queue.

### Step 1: Debounce check

```cpp
if (has_last_edge_)
{
    const std::uint32_t elapsed_ms = edge.timestamp_ms - last_edge_timestamp_ms_;
    if (elapsed_ms < kDebounceMs)
    {
        return std::nullopt;
    }
}
```

If we have seen a previous edge AND the new edge arrived within 20 ms, discard it (return `nullopt`). This filters out mechanical bounce.

`has_last_edge_` is `false` on the very first call — we skip debounce for the first-ever edge to avoid discarding a valid initial press.

### Step 2: Record this edge

```cpp
has_last_edge_ = true;
last_edge_timestamp_ms_ = edge.timestamp_ms;
```

Save this edge's timestamp so we can debounce future edges against it.

### Step 3: Handle press (going down)

```cpp
if (edge.type == RawButtonEdgeType::Pressed)
{
    if (is_pressed_)
    {
        return std::nullopt;  // already pressed — duplicate signal
    }
    is_pressed_ = true;
    press_started_ms_ = edge.timestamp_ms;
    return std::nullopt;  // don't classify yet — wait for release
}
```

When the button goes down, we just record when it started. We don't produce an event yet — we need to wait for the release to know whether it was a short or long press.

If `is_pressed_` is already true and we see another press edge, it is a duplicate (hardware glitch) — ignore it.

### Step 4: Handle release (going up)

```cpp
if (!is_pressed_)
{
    return std::nullopt;  // spurious release with no matching press
}

is_pressed_ = false;

const std::uint32_t hold_time_ms = edge.timestamp_ms - press_started_ms_;
const EventType event_type =
    (hold_time_ms >= kLongPressThresholdMs) ? EventType::ButtonLongPress
                                            : EventType::ButtonShortPress;

return Event{event_type, edge.timestamp_ms};
```

When the button is released:
1. Check that we actually recorded a press start (`is_pressed_`). If not, ignore the spurious release.
2. Calculate `hold_time_ms = release_time - press_start_time`
3. If held ≥ 500 ms → `ButtonLongPress`; otherwise → `ButtonShortPress`
4. Return the event. The `StateMachineTask` will dispatch it.

---

## Example timeline

```
t=0 ms:   Pressed  → debounce ok, is_pressed_=true, press_started_ms_=0, return nullopt
t=5 ms:   Pressed  → elapsed 5 ms < 20 ms → debounce REJECT
t=250 ms: Released → hold=250 ms < 500 ms → ButtonShortPress at t=250

t=0 ms:   Pressed  → debounce ok, press_started_ms_=0, return nullopt
t=600 ms: Released → hold=600 ms ≥ 500 ms → ButtonLongPress at t=600
```
