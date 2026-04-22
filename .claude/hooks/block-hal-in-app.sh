#!/usr/bin/env bash
# PreToolUse hook: block edits that introduce a direct STM32 HAL include
# into edge/app/ (except the Phase 1a file main.cpp).
#
# Enforces the Dependency Inversion Principle from principles/SOLID_PRINCIPLES.md:
# application code must depend on abstract C++ interfaces, not stm32f4xx_hal_*.h.
#
# Exit 0 = allow. Exit 2 = block (stderr is shown to Claude).

set -euo pipefail

input="$(cat)"

file_path="$(printf '%s' "$input" | jq -r '.tool_input.file_path // ""')"
# For Edit the new content is .new_string; for Write it's .content.
payload="$(printf '%s' "$input" | jq -r '(.tool_input.new_string // .tool_input.content // "")')"

# Only police edge/app/, and allow the Phase-1a main.cpp concession.
case "$file_path" in
  */edge/app/*)
    case "$file_path" in
      */edge/app/main.cpp) exit 0 ;;
    esac
    ;;
  *)
    exit 0
    ;;
esac

if printf '%s' "$payload" | grep -qE '#[[:space:]]*include[[:space:]]*[<"]stm32f4xx_hal'; then
  cat >&2 <<EOF
Blocked: direct STM32 HAL include is not permitted under edge/app/ (except main.cpp).

This violates the Dependency Inversion Principle documented in
principles/SOLID_PRINCIPLES.md: application-layer code must depend on
abstract C++ interfaces (e.g. ICommunication, IGpio), never on
stm32f4xx_hal_*.h directly. Wrap the HAL in a driver class under
edge/bsp/ or edge/drivers/ and include the abstract interface instead.

File: $file_path
EOF
  exit 2
fi

exit 0
