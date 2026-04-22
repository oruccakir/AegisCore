# Repository Guidelines

## Project Structure & Module Organization
`edge/` contains the active STM32F407G-DISC1 firmware. Put application code in `edge/app/`, board support and IRQ glue in `edge/bsp/`, startup assembly in `edge/startup/`, linker scripts in `edge/linker/`, and generated artifacts in `edge/build/`. Treat `edge/vendor/STM32CubeF4/` as read-only vendor code. Requirements, traceability, and interface specs live under `docs/`; engineering rules and design references live under `principles/`.

## Build, Test, and Development Commands
- `git submodule update --init --recursive` fetches STM32CubeF4 before the first build.
- `cmake -S edge -B edge/build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake` configures the cross-build.
- `cmake --build edge/build` builds `aegiscore-edge.elf`, `.bin`, and `.hex`.
- `st-info --probe` checks ST-Link connectivity.
- `st-flash write edge/build/aegiscore-edge.bin 0x8000000` flashes the board.
- `cd docs/SRD && latexmk -xelatex srd.tex` rebuilds the SRD PDF when documentation changes.

## Coding Style & Naming Conventions
Match the existing embedded style: 4-space indentation, clear file-level responsibilities, and minimal dependencies in first-party code. App code is C++20; BSP and startup code remain C/ASM where required. Runtime dynamic allocation, exceptions, and RTTI are out of bounds. Add new HAL modules explicitly in `edge/CMakeLists.txt`; do not glob vendor sources. Keep STM32 and HAL filenames aligned with upstream naming, and reference requirement IDs in uppercase form such as `ARCH-02` or `FR-05`.

## Testing Guidelines
Automated verification is documented in `docs/VnV/verification-plan.md`, but the current repository primarily validates changes through clean builds and on-board checks. Before opening a PR, rerun `cmake --build edge/build`. For firmware changes, flash the board and record the observed result, such as LED timing or peripheral behavior. When adding tests later, keep names tied to the subsystem or requirement they verify.

## Commit & Pull Request Guidelines
Recent history follows short, imperative subjects with Conventional Commit-style prefixes such as `feat:` and `docs:`. Keep commits scoped to one change. PRs should summarize the hardware impact, list the commands you ran, and link the relevant requirement or spec section in `docs/` when behavior changes. Include photos, logs, or captures only when they clarify hardware-visible results.
