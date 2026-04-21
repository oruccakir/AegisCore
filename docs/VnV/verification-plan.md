# AC2 Verification & Validation Plan

**Document ID:** AC2-VnV-001
**Version:** 2.0.0
**Parent:** AC2-SRD-001 §9 (VR), `10_traceability.tex`

## 1. Purpose

Bu plan AC2 sistemi için gereksinim doğrulama (verification) ve operasyonel geçerlilik (validation) stratejisini tanımlar. V&V'nin çıktısı her release için bir **Verification Report** dosyasıdır (bkz. §8).

## 2. V&V Activities Overview

| Faz | Aktivite | Giriş | Çıkış |
|-----|---------|-------|-------|
| Build | Unit test, static analysis, lint | Kaynak kod | CI pass + coverage report |
| Integration | Gateway-Edge HIL testleri | Firmware + Gateway binary | JUnit XML |
| System | Acceptance Test Suite (ATS) | Full stack | Signed ATS report |
| Soak | 168h continuous run | Full stack | Uptime log |
| Security | Fuzz, dependency audit, pen-test (manual) | Binaries + SBOM | Security review sign-off |
| Release | Checklist review | All above | Release note |

## 3. Test Levels

### 3.1 Unit Tests (Host-Side)
- **Framework:** GoogleTest 1.14+, GoogleMock.
- **Location:** `tests/unit/`.
- **Target coverage:** line ≥ 90%, branch ≥ 85%, MC/DC ≥ 80% (critical modules).
- **Naming:** `TEST(ClassName, Behavior)`; no shared state between tests.
- **Mocks:** HAL fonksiyonları için `HalMock` (FakeIt) veya elle yazılmış fake sınıflar.
- **Run:** CI her commit'te + `make test` lokal.

### 3.2 Static Analysis
- **cppcheck** (MISRA addon, severity medium+): blocking.
- **Clang-Tidy** (cert-, cppcoreguidelines-, bugprone-, performance-): blocking.
- **IWYU:** include hygiene, informational.
- **CodeQL:** opsiyonel, haftalık.
- Deviation'lar `docs/deviations.md`'de gerekçelendirilir; onaysız deviation CI kırar.

### 3.3 Integration Tests (HIL)
- **Framework:** pytest + pytest-asyncio + pyserial.
- **Location:** `tests/hil/`.
- **Fixture:** ST-Link ile hedef kart flash edilir; USB-UART üzerinden Gateway simüle edilir.
- **Scenario coverage:**
  - Boot → IDLE (ATS-01).
  - Her `CMD_SET_STATE` geçişi (valid + invalid).
  - Button short/long press timing.
  - Heartbeat loss → degraded → disconnected.
  - CRC hata injection (her frame'in bir byte'ı flip edilir → drop count doğrulanır).
  - HMAC hata injection → ERR_AUTH_FAIL.
  - Replay injection → ERR_REPLAY.
- **Run:** CI self-hosted runner (target board attached).

### 3.4 System / Acceptance Tests (ATS)
Her release öncesi ATS-01..07 koşturulur (bkz. SRD §9.6). Sonuç matrisi:

| ATS | Pass Kriteri | Araç |
|-----|--------------|------|
| ATS-01 | Boot → IDLE ≤ 500ms | GPIO trigger + scope |
| ATS-02 | 10 press no ghost | pytest-hil |
| ATS-03 | LED 500±10ms | scope / logic analyzer |
| ATS-04 | p95 latency ≤ 50ms | Playwright + UART timestamp |
| ATS-05 | Cable unplug → 3s disconnect | pytest-hil |
| ATS-06 | Bad HMAC → drop + audit | pytest-hil |
| ATS-07 | 1h heap flat | soak harness |

### 3.5 Soak Test
- 168h (1 hafta) continuous çalışma, random command injection (1 req/dk).
- Metrics: CPU load, free heap, stack high-water, UART err count, WS reconnect count.
- Grafana dashboard live-monitoring.
- Pass: zero watchdog reset, zero HardFault, heap drift < 1 byte, latency SLO sağlanır.

### 3.6 Fuzz Testing
- **Target:** AC2 frame parser (host-side libFuzzer build).
- **Corpus:** manual seed frames + auto-grown.
- **Sanitizers:** ASan + UBSan.
- **Duration:** 60s smoke per CI run + 8h overnight weekly.
- Crash/timeout/UBSan report = blocking bug.

### 3.7 Security Testing
- Dependency audit (`npm audit`, `cargo audit` if applicable) blocking on high.
- SBOM diff between releases reviewed.
- Manual pen-test (light): CSRF test, JWT replay, missing-auth probe. Findings filed as issues.

### 3.8 UI/UX Validation
- Playwright ile smoke E2E (login, connect, set_state, disconnect).
- axe-core erişilebilirlik denetimi (NFR-USE-01).
- Visual regression: Percy veya Chromatic (opsiyonel).

## 4. Tool Chain

| Tool | Version | Role |
|------|---------|------|
| arm-none-eabi-gcc | ≥ 13 | Firmware build |
| CMake | ≥ 3.22 | Build system |
| GoogleTest | 1.14+ | Unit tests |
| cppcheck | 2.13+ | Static analysis |
| clang-tidy | 17+ | Lint |
| pytest | 8+ | HIL + integration |
| Playwright | 1.44+ | E2E |
| libFuzzer | LLVM | Fuzz |
| Docker | 24+ | Reproducible build |

## 5. Environments

| Env | Purpose | Accessibility |
|-----|---------|---------------|
| Dev | Geliştirici lokal | hot-reload, fast feedback |
| CI | Automated gate | GitHub Actions, self-hosted runner for HIL |
| Soak-Rig | 168h test | dedicated board + monitoring |
| Demo | Live demonstration | controlled |

## 6. Entry / Exit Criteria

### Entry (test başlayabilir)
- Build successful, no warnings.
- Unit tests pass.
- Deviations reviewed.

### Exit (test tamamlandı kabul edilir)
- ATS pass rate 100%.
- Coverage hedefleri sağlanır.
- Blocker/critical bug sayısı 0.
- Traceability matrix coverage %100.

## 7. Roles & Responsibilities

| Role | Responsibility |
|------|----------------|
| Developer | Unit tests, lint clean, traceability link-back |
| QA | HIL harness bakımı, ATS koşturma |
| Security Reviewer | SR-* gereksinimlerin imzalanması |
| Release Manager | Release checklist, DEP-04 |

## 8. Verification Report Template

Her release için:
- Release tag + git SHA.
- Toolchain image digest.
- Test result özetleri (unit, HIL, ATS).
- Coverage tablosu.
- Bilinen sınırlamalar (known limitations).
- Güvenlik review sign-off.
- Soak test süreleri (son 7 gün).
- İmzalar (developer, QA, security reviewer, release manager).

## 9. Change Control

Bu plan yarı-yıllık review edilir. Scope/standard değişikliğinde hemen revize edilir; sürüm numarası artırılır (`VnV-MAJOR.MINOR`).
