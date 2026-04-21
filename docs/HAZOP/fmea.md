# AC2 Hazard Analysis & FMEA

**Document ID:** AC2-HAZOP-001
**Version:** 2.0.0
**Parent Requirements:** SAF-01..SAF-10 (see `docs/SRD/sections/08_safety.tex`)

## 1. Scope

Bu doküman AC2 sisteminin bilinen tehlike/arıza modlarını, etkilerini ve her birine karşılık gelen mitigation'ı (tasarım gereksinimi) listeler. Sistem bir demonstrasyon platformu olduğundan formal bir SIL ataması yapılmaz; ancak her tehlike kalitatif olarak **Severity** × **Likelihood** matrisinde değerlendirilir.

## 2. Severity & Likelihood Scale

| Level | Severity | Likelihood |
|-------|----------|-----------|
| 1 | Kozmetik/rahatsızlık | < 1 yılda bir |
| 2 | Fonksiyon kaybı, demo akışı bozulur | Ayda bir |
| 3 | Yanlış görselleştirme, operatör yanılgısı | Haftada bir |
| 4 | Sistem restart gerekir | Günde bir |
| 5 | Sistem unbounded davranır (dead-lock, corrupted state) | Birden fazla |

**Risk** = Severity × Likelihood. 12+ = **High** (mitigation zorunlu). 6-11 = **Medium**. ≤ 5 = **Low** (accept).

## 3. Hazard Log

| ID | Hazard / Failure Mode | Cause | Effect | Sev | Like | Risk | Mitigation | Req |
|----|----------------------|-------|--------|-----|------|------|-----------|-----|
| H1 | Infinite loop in main task | Software bug | Sistem cevap vermez | 4 | 3 | 12 | IWDG 1s timeout + reset | SAF-01 |
| H2 | HardFault (null-pointer, div-by-zero) | Defensive coding miss | Firmware freeze | 5 | 2 | 10 | Fault handler + panic block + FAIL-SAFE | SAF-02, SAF-06 |
| H3 | Task starvation (priority inversion) | FreeRTOS misconfiguration | Missed deadlines | 4 | 2 | 8 | Priority ceiling + mutex, watchdog guard | SAF-01, ARCH-03 |
| H4 | Bit flip in state variable | EMI/soft error | State "impossible" değere gider | 3 | 2 | 6 | State shadow + CRC check per tick + FAIL-SAFE on mismatch | SAF-02 |
| H5 | Voltage brown-out | Power supply drop | Undefined behavior | 5 | 2 | 10 | BOR Level 2 (2.7V) | SAF-03 |
| H6 | Serial cable disconnected during TRACK | Operator / mekanik | Edge kör çalışır, UI blind | 3 | 4 | 12 | Heartbeat loss detection → TRACK then IDLE | SAF-04 |
| H7 | Heartbeat loss > 5s | I/F A failure | UI yanıltıcı bilgi gösterebilir | 4 | 3 | 12 | FAIL-SAFE + UI disconnected banner | SAF-02, SAF-04, FR-07 |
| H8 | Task stack overflow | Undersized stack | Memory corruption | 5 | 2 | 10 | FreeRTOS overflow hook + high-water watermark telemetry | SAF-05 |
| H9 | Unhandled exception propagates | No-exceptions rule violation | Undefined | 5 | 1 | 5 | `-fno-exceptions` + CI lint | ARCH-02 |
| H10 | Flash corruption (silent) | ECC-less region, aging | Silently wrong code runs | 5 | 1 | 5 | Flash CRC BIT on boot | SAF-07 |
| H11 | Null-pointer dereference | Bug | HardFault | 4 | 2 | 8 | MPU null-guard region | SAF-08 |
| H12 | Button bouncing ghost presses | Hardware | Spurious state changes | 2 | 4 | 8 | Debounce + pattern filter | SAF-09 |
| H13 | UART RX buffer overflow | Burst flood | Frame loss | 3 | 3 | 9 | DMA circular buffer + IDLE IRQ + rate limit | SR-05 |
| H14 | Replay attack | Malicious I/F-A tap | Unauthorized command executed | 5 | 2 | 10 | Monotonic seq + HMAC | SR-01, SR-02 |
| H15 | Stale data in UI | WebSocket backlog | Operator yanıltıcı görü | 3 | 3 | 9 | Heartbeat-based connection indicator + stale markers | FR-07 |
| H16 | CSRF attack on control API | Browser session theft | Unauthorized command | 4 | 2 | 8 | Bearer token in header + SameSite=Strict cookies | SR-04, SR-10 |
| H17 | XSS via log terminal | Unsanitized log content | Session hijack | 4 | 2 | 8 | Strict CSP + DOM text escape | SR-10 |
| H18 | Credentials leaked via git | Developer mistake | Demo PSK exposed | 3 | 2 | 6 | Secret scanning in CI + pre-commit hook | SR-09 |
| H19 | Firmware downgrade attack | Physical flash access | Older vulnerable version loaded | 3 | 1 | 3 | Anti-rollback counter (v2.1 roadmap) | SR-07 |
| H20 | Supply chain compromise (npm) | Upstream package | Malicious code in gateway | 4 | 1 | 4 | `npm audit` + SBOM + pinned versions | SR-09 |
| H21 | Simulation engine feeding attacker goal states | Manipulation of seed | Predictable demo outcome | 2 | 2 | 4 | Seed compile-time constant; not runtime-writable | ARCH-04 |
| H22 | Log flood DoS | High-rate fault chain | Disk fill, audit log rotation thrash | 3 | 2 | 6 | Log rate limit + rotation + retention policy | SR-06 |
| H23 | UI click-race (double START) | Operator fast-click | Duplicate commands | 2 | 3 | 6 | UI idempotency + button disabled until ACK | FR-04 |
| H24 | FAIL-SAFE lockup (no exit) | Reset-ACK logic bug | Demo unrecoverable without power cycle | 3 | 1 | 3 | Watchdog ensures bounded reset + CMD_RESET_ACK path | SAF-02 |
| H25 | Clock drift causes CRC mismatches | HSE absent, HSI inaccurate | Communication fails | 3 | 1 | 3 | HSE preferred + HSI fallback flag in boot banner | SAF-07 |

## 4. Residual Risk

- **High (12+):** H1, H6, H7. Hepsi watchdog + heartbeat + FAIL-SAFE triad'ı ile azaltılmıştır.
- **Medium (6-11):** 13 adet. Her biri en az bir SR/SAF requirement'ına bağlanmıştır.
- **Low (≤5):** 9 adet. Kabul edilebilir; demo sisteminin riskini değiştirmez.

## 5. Review Cadence

Hazard log her major release öncesi review edilir. Yeni bir feature eklendiğinde ilgili ekip en az 1 hazard entry eklemeyi (veya "no new hazards identified" justification'ı yapmayı) taahhüt eder.
