# RateLimiter — Token Bucket Rate Limiting

**Files:** `edge/app/rate_limiter.hpp`, `edge/app/rate_limiter.cpp`

---

## What this module is

Without rate limiting, a malicious or buggy gateway could flood the STM32 with thousands of `set_state` commands per second — potentially causing instability or wearing out state transition logic.

`RateLimiter` uses the **token bucket algorithm** to allow bursts up to a limit while enforcing a long-term rate ceiling.

---

## Token bucket algorithm

Imagine a bucket that holds tokens:
- The bucket has a maximum capacity (burst limit)
- Tokens are added at a fixed rate over time (e.g., 5 tokens per second)
- Each request consumes one token
- If the bucket is empty, the request is denied

This allows short bursts (if tokens are available) but prevents sustained high rates.

---

## Per-command buckets

```cpp
Bucket set_state_  = {0U, 10000U, 5000U, 10000U};
Bucket manual_lock_= {0U, 3000U,  2000U,  3000U};
```

The `Bucket` struct uses **fixed-point arithmetic** where one token = 1000 units:

For `CMD_SET_STATE`:
- `last_refill_ms = 0` — start time
- `tokens = 10000` — starts full (10 tokens × 1000)
- `rate_per_sec = 5000` — adds 5 tokens per second (5000 units/s)
- `burst_tokens = 10000` — maximum 10 tokens (10000 units)

So: up to 10 rapid state changes are allowed in a burst, then only 5 per second sustained.

For `CMD_MANUAL_LOCK`:
- `tokens = 3000` — starts full (3 tokens)
- `rate_per_sec = 2000` — 2 tokens per second
- `burst_tokens = 3000` — max 3 tokens

---

## `Refill(Bucket& b, std::uint32_t now_ms)`

```cpp
void RateLimiter::Refill(Bucket& b, std::uint32_t now_ms) noexcept
{
    const std::uint32_t elapsed_ms = now_ms - b.last_refill_ms;
    if (elapsed_ms == 0U) { return; }

    const std::uint32_t added = b.rate_per_sec * elapsed_ms / 1000U;
    b.tokens = (b.tokens + added > b.burst_tokens) ? b.burst_tokens
                                                    : b.tokens + added;
    b.last_refill_ms = now_ms;
}
```

**Line by line:**

`elapsed_ms = now_ms - b.last_refill_ms` — how many milliseconds have passed since the last refill.

`if (elapsed_ms == 0U) return` — avoid division by zero or useless computation.

`added = rate_per_sec * elapsed_ms / 1000U` — tokens to add. Example: if 500 ms have elapsed at 5000 units/sec rate: `5000 * 500 / 1000 = 2500` units (= 2.5 tokens). Integer division truncates — this is intentional and slightly conservative.

`b.tokens = min(b.tokens + added, b.burst_tokens)` — cap at the burst maximum. If the bucket is already full, extra tokens are discarded.

`b.last_refill_ms = now_ms` — advance the refill timestamp to now.

---

## `Allow(std::uint8_t cmd_id, std::uint32_t now_ms)` → `bool`

```cpp
bool RateLimiter::Allow(std::uint8_t cmd_id, std::uint32_t now_ms) noexcept
{
    Bucket* bucket = nullptr;

    if (cmd_id == CmdId::kSetState)        { bucket = &set_state_; }
    else if (cmd_id == CmdId::kManualLock) { bucket = &manual_lock_; }
    else { return true; }  // no rate limit on other commands

    Refill(*bucket, now_ms);

    if (bucket->tokens < 1000U) { return false; }  // empty bucket

    bucket->tokens -= 1000U;  // consume one token
    return true;
}
```

**Line by line:**

Select the appropriate bucket based on command ID. Other commands (`kGetVersion`, `kHeartbeat`) are not rate-limited.

`Refill(*bucket, now_ms)` — add tokens for time elapsed since last call.

`if (bucket->tokens < 1000U)` — 1000 units = one token. If fewer than one token available, deny the request.

`bucket->tokens -= 1000U` — consume one token.

---

## Why fixed-point?

Floating point (`float`, `double`) is not allowed in this codebase (MISRA subset). Fixed-point arithmetic with a scale factor of 1000 gives sub-token precision — we can represent 0.5 tokens as 500 units — without floating point.

The cost of not having sub-token precision (just using integer tokens) would be: 500 ms pass at 1 token/s → integer says 0 tokens added (0.5 truncated to 0) → slightly too conservative. With 1000× scaling: 500 * 1000 / 1000 = 500 units added → 0.5 tokens → still conservative on the final `< 1000` check but more accurate over time.
