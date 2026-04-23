#!/usr/bin/env bash
set -euo pipefail

RED=$'\033[0;31m'; GREEN=$'\033[0;32m'; YELLOW=$'\033[1;33m'
CYAN=$'\033[0;36m'; BOLD=$'\033[1m'; RESET=$'\033[0m'

SERIAL_PORT="${SERIAL_PORT:-/dev/ttyACM0}"
LOG_LEVEL="${LOG_LEVEL:-debug}"
SKIP_FLASH="${SKIP_FLASH:-0}"
GW_PID=0
UI_PID=0

log()  { echo -e "${BOLD}[dev.sh]${RESET} $*"; }
ok()   { echo -e "${GREEN}[dev.sh] ✓ $*${RESET}"; }
fail() { echo -e "${RED}[dev.sh] ✗ $*${RESET}"; exit 1; }
warn() { echo -e "${YELLOW}[dev.sh] ! $*${RESET}"; }

cleanup() {
    echo ""
    warn "Shutting down..."
    [[ $UI_PID -ne 0 ]] && kill "$UI_PID" 2>/dev/null || true
    [[ $GW_PID -ne 0 ]] && kill "$GW_PID" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# ── 1. Build edge firmware ────────────────────────────────────────────────
log "Building edge firmware..."
cmake -S edge -B edge/build -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    --log-level=ERROR -Wno-dev 2>/dev/null
cmake --build edge/build 2>&1 \
    | grep -E "error:|FLASH:|RAM:|ninja: no work" \
    | sed "s/^/${CYAN}[edge]${RESET} /"
ok "Edge firmware built"

# ── 2. Flash STM32 ────────────────────────────────────────────────────────
if [[ "$SKIP_FLASH" == "1" ]]; then
    warn "Skipping flash (SKIP_FLASH=1)"
else
    log "Flashing STM32 via ST-Link..."
    if ! st-info --probe 2>&1 | grep -q "Found 1 stlink"; then
        fail "ST-Link not found. Connect STM32 via USB."
    fi
    st-flash erase 2>&1 | grep -E "success|error|Error" | sed "s/^/${CYAN}[flash]${RESET} /"
    st-flash write edge/build/aegiscore-edge.bin 0x8000000 2>&1 \
        | grep -E "jolly|ERROR|error" \
        | sed "s/^/${CYAN}[flash]${RESET} /"
    ok "Firmware flashed"
    sleep 1
fi

# ── 3. Check serial port ──────────────────────────────────────────────────
if [[ ! -e "$SERIAL_PORT" ]]; then
    fail "Serial port $SERIAL_PORT not found. Set SERIAL_PORT= or check Arduino bridge."
fi
if [[ ! -w "$SERIAL_PORT" ]]; then
    warn "No write permission on $SERIAL_PORT, fixing..."
    sudo chmod a+rw "$SERIAL_PORT" 2>/dev/null || fail "Permission denied on $SERIAL_PORT"
fi
ok "Serial port $SERIAL_PORT ready"

# ── 4. Build gateway ──────────────────────────────────────────────────────
log "Building gateway TypeScript..."
(cd gateway && npm run build 2>&1 | grep -v "^$" | sed "s/^/${CYAN}[gateway]${RESET} /")
ok "Gateway built"

# ── 5. Build UI ───────────────────────────────────────────────────────────
log "Building UI..."
(cd ui && npm install --silent 2>/dev/null && npm run build 2>&1 \
    | grep -E "error|Route|✓" | sed "s/^/${CYAN}[ui]${RESET} /")
ok "UI built"

# ── 6. Start gateway ──────────────────────────────────────────────────────
log "Starting gateway (LOG_LEVEL=$LOG_LEVEL)..."
(cd gateway && SERIAL_PORT="$SERIAL_PORT" LOG_LEVEL="$LOG_LEVEL" node dist/index.js 2>&1 \
    | sed "s/^/${GREEN}[gateway]${RESET} /") &
GW_PID=$!

sleep 1
if ! kill -0 "$GW_PID" 2>/dev/null; then
    fail "Gateway failed to start"
fi
ok "Gateway started"

# ── 7. Start UI ───────────────────────────────────────────────────────────
log "Starting UI on http://localhost:3000 ..."
fuser -k 3000/tcp 2>/dev/null || true
(cd ui && node node_modules/.bin/next start -p 3000 2>&1 \
    | sed "s/^/${CYAN}[ui]${RESET} /") &
UI_PID=$!

sleep 3
if ! kill -0 "$UI_PID" 2>/dev/null; then
    fail "UI failed to start"
fi

echo ""
echo -e "${BOLD}══════════════════════════════════════════${RESET}"
echo -e "${BOLD} System live. Ctrl+C to stop.${RESET}"
echo -e "${BOLD} Dashboard  → http://localhost:3000${RESET}"
echo -e "${BOLD} WebSocket  → ws://localhost:8443${RESET}"
echo -e "${BOLD}══════════════════════════════════════════${RESET}"
echo ""

wait "$GW_PID"
