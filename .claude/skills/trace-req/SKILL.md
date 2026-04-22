---
name: trace-req
description: Look up an AegisCore requirement ID (ARCH/FR/NFR/SR/SAF/VR/DEP-*) in docs/SRD, IRS, HAZOP, and V&V plan. Returns the requirement text, related hazards/tests, existing code touching it, and a draft commit/PR footer linking back to the ID.
disable-model-invocation: true
---

# trace-req — Requirement traceability helper

User invocation: `/trace-req <REQ-ID>` (e.g. `/trace-req FR-UART-05`, `/trace-req NFR-MAINT-01`, `/trace-req SR-AUTH-02`).

## What this skill does

Given a requirement ID, produce a single report containing:

1. **Canonical requirement text** — quote the exact line(s) from the SRD/IRS/HAZOP/VnV.
2. **Source location** — file path + line number where the requirement is defined.
3. **Related requirements** — cross-references (parent/child/rationale links).
4. **Associated hazards** — any entries in `docs/HAZOP/fmea.md` that cite this ID.
5. **Verification method** — the ATS-* entry or test type from `docs/VnV/verification-plan.md`.
6. **Implementing code** — grep the repo for the ID in comments, commit messages, PR bodies; list files that implement it.
7. **Commit footer draft** — a copy-pasteable conventional-commit footer linking the change to the ID.

## Procedure

1. **Identify the ID class** from the prefix:
   - `ARCH-*` → `docs/SRD/sections/04_arch.tex`
   - `FR-*`   → `docs/SRD/sections/05_fr.tex`
   - `NFR-*`  → `docs/SRD/sections/06_nfr.tex`
   - `SR-*`   → `docs/SRD/sections/07_security.tex`
   - `SAF-*`  → `docs/SRD/sections/08_safety.tex`
   - `VR-*` / `ATS-*` → `docs/SRD/sections/09_verification.tex` + `docs/VnV/verification-plan.md`
   - `H<n>` (e.g. `H12`) → `docs/HAZOP/fmea.md`
   - `DEP-*` → `docs/VnV/verification-plan.md` §7 / release checklist

2. **Extract the requirement** with precise line numbers:
   ```bash
   grep -n "<REQ-ID>" docs/SRD/sections/*.tex docs/IRS/*.tex docs/HAZOP/*.md docs/VnV/*.md
   ```

3. **Pull the canonical text.** Read the file at the match and extract the full requirement block (often 2–6 lines in LaTeX — include `\req{}{}`, surrounding prose, and any `\rationale{}`).

4. **Find traceability links** in `docs/SRD/sections/10_traceability.tex`:
   ```bash
   grep -n "<REQ-ID>" docs/SRD/sections/10_traceability.tex
   ```

5. **Find related hazards / tests:**
   ```bash
   grep -rn "<REQ-ID>" docs/HAZOP docs/VnV
   ```

6. **Find implementing code** — the ID appears in comments, `@req` tags, commit messages:
   ```bash
   grep -rn "<REQ-ID>" --include='*.c' --include='*.cpp' --include='*.h' --include='*.hpp' edge/
   git log --all --grep="<REQ-ID>" --oneline
   ```

7. **Emit the report** using the template below.

## Output template

```markdown
# <REQ-ID>

**Source:** `docs/SRD/sections/06_nfr.tex:47`
**Class:** NFR (maintainability)

## Canonical text
> NFR-MAINT-01: All first-party source shall compile clean with -Wall -Wextra
> -Wpedantic -Werror. Vendor HAL is exempt.

## Rationale
> Zero-warning discipline is an early indicator of undefined behavior. ...

## Cross-references
- Parent: ARCH-01 (toolchain discipline)
- Verified by: ATS-CI-01
- Hazard coverage: (none direct)

## Verification
- **Method:** static analysis + CI gate (cppcheck, clang-tidy, -Werror compile).
- **Test entry:** `docs/VnV/verification-plan.md` §3.2.

## Implementing code
- `edge/CMakeLists.txt:71-85` — compile options
- `edge/CMakeLists.txt:91-95` — -Werror scoping

## Commit footer draft
```
Refs: NFR-MAINT-01
Verified-By: ATS-CI-01
```
```

## Edge cases

- **ID not found:** print `"<REQ-ID>" not found in docs/ — check spelling or whether the spec has been revised.` Do not guess.
- **Multiple definitions:** list all matches; flag as a possible spec bug (IDs should be unique).
- **No implementing code yet:** say "no implementing code in repo yet — this is a scheduled requirement".

## Non-goals

- Do not write the commit itself. Just emit the footer text.
- Do not modify the spec files. If the requirement is unclear, say so.
- Do not invent requirements that don't exist.
