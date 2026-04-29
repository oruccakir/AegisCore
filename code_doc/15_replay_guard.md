# ReplayGuard — Sequence Number Replay Protection

**Files:** `edge/app/replay_guard.hpp`, `edge/app/replay_guard.cpp`

---

## What this module is

A **replay attack** works like this: an attacker intercepts a valid command (e.g., "set state = TRACK") and retransmits it later to make the system do something unintended. Even if the HMAC is valid (because the attacker replays the entire frame including its HMAC), the command should be rejected.

`ReplayGuard` prevents this by tracking the last accepted sequence number. Any frame with a sequence number ≤ the last accepted one is rejected as a replay.

---

## How it works

The gateway assigns a strictly increasing sequence number to every command it sends. The STM32 firmware tracks the last number it accepted.

Example:
```
Gateway sends: seq=1 → accepted, last_seq_=1
Gateway sends: seq=2 → accepted, last_seq_=2
Attacker replays: seq=1 → rejected (1 <= 2)
Gateway sends: seq=3 → accepted, last_seq_=3
```

---

## State variables

```cpp
std::uint32_t last_seq_    = 0U;     // last accepted sequence number
bool          initialized_ = false;  // has the first frame been seen?
```

`initialized_` is needed because sequence numbers start at some arbitrary value chosen by the gateway (could be 1, could be 0). We cannot assume the first frame always has `seq=1`. So we accept the first frame unconditionally to seed `last_seq_`, and reject anything that doesn't advance from there.

---

## `Check(std::uint32_t seq)` → `bool`

```cpp
bool ReplayGuard::Check(std::uint32_t seq) noexcept
{
    if (!initialized_)
    {
        last_seq_    = seq;
        initialized_ = true;
        return true;   // accept unconditionally
    }

    if (seq <= last_seq_)
    {
        return false;  // replay or out-of-order — reject
    }

    last_seq_ = seq;
    return true;       // accept and advance
}
```

**First call (`!initialized_`):** Accept the frame unconditionally and record its sequence number as the baseline. This handles the case where the gateway starts from any arbitrary sequence number.

**Subsequent calls:**
- `seq > last_seq_` → accept and update `last_seq_`
- `seq <= last_seq_` → reject (replay or out-of-order)

The guard uses `<=` not `<` — even an equal sequence number is rejected. This handles the case where the gateway accidentally sends two frames with the same sequence number (firmware bug or hardware glitch).

---

## `Reset()`

```cpp
void Reset() noexcept { last_seq_ = 0U; initialized_ = false; }
```

Resets to initial state. This is called if the guard needs to be re-seeded (e.g., after a gateway reconnection). Currently not called anywhere but available for future use.

---

## Limitations of this approach

1. **Sequence number wrap**: `uint32_t` can hold ~4 billion values. At 5 commands/second, wrap happens after ~27 years — not a practical concern.

2. **Reboot resets state**: After an MCU reset, `initialized_` is `false`. The attacker could wait for a reboot and replay old commands. The SRD acknowledges this limitation (SR-02 "resets on reboot"). A full solution requires persistent storage of the last sequence number in Flash or EEPROM.

3. **No window**: Frames must arrive strictly in order. If the gateway sends seq=5 before seq=4 arrives (due to UART noise causing seq=4 to be dropped), seq=4 will be permanently blocked. In practice, this rarely matters on a wired UART link.
