#!/usr/bin/env bash
# PostToolUse hook: incremental firmware build after any edit to C/C++/CMake sources.
#
# Runs only if edge/build has been configured (we do not auto-configure).
# Surfaces -Werror breakage early — NFR-MAINT-01 (zero warnings / warnings-as-errors).
#
# Exit 0 = success. Exit 2 = failure (stderr visible to Claude; Claude will react).

set -u

input="$(cat)"
file_path="$(printf '%s' "$input" | jq -r '.tool_input.file_path // ""')"

case "$file_path" in
  *.c|*.cpp|*.cc|*.cxx|*.h|*.hpp|*/CMakeLists.txt|*.cmake|*.ld|*.s|*.S) ;;
  *) exit 0 ;;
esac

# Only rebuild firmware if the edit touched the edge/ tree.
case "$file_path" in
  */edge/*) ;;
  *) exit 0 ;;
esac

if [ ! -d edge/build ]; then
  # Not yet configured — stay silent, do not auto-configure.
  exit 0
fi

# Incremental Ninja build. Capture output for surfacing on failure.
out="$(cmake --build edge/build 2>&1)"
rc=$?
if [ $rc -ne 0 ]; then
  printf '%s\n' "$out" | tail -80 >&2
  echo "" >&2
  echo "Build failed (rc=$rc). Fix -Werror/link errors before continuing." >&2
  exit 2
fi

exit 0
