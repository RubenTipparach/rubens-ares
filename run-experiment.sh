#!/usr/bin/env bash
# Usage: ./run-experiment.sh <label> [duration_sec]
# Builds, starts profile server, reloads client, waits, kills server.
LABEL="${1:-unlabeled}"
DURATION="${2:-60}"

echo "=== experiment: $LABEL ($DURATION sec) ==="

# Force-rebuild touched source files
touch /c/Users/santi/repos/rubens-ares/wasm-patches/web-ui/main.cpp
touch /c/Users/santi/repos/rubens-ares/wasm-patches/ares/n64/rdp/render.cpp
touch /c/Users/santi/repos/rubens-ares/wasm-patches/ares/n64/cpu/cpu.cpp
touch /c/Users/santi/repos/rubens-ares/wasm-patches/ares/n64/rsp/rsp.cpp
touch /c/Users/santi/repos/rubens-ares/wasm-patches/ares/n64/ai/ai.cpp
rm -f /c/Users/santi/repos/rubens-ares/build/web-ui/obj/web-ui.o
rm -f /c/Users/santi/repos/rubens-ares/build/web-ui/obj/ares-n64-rdp.o
rm -f /c/Users/santi/repos/rubens-ares/build/web-ui/obj/ares-n64-cpu.o
rm -f /c/Users/santi/repos/rubens-ares/build/web-ui/obj/ares-n64-rsp.o
rm -f /c/Users/santi/repos/rubens-ares/build/web-ui/obj/ares-n64-ai.o
rm -f /c/Users/santi/repos/rubens-ares/build/web-ui/obj/alp-bridge.o

# Build
echo "[build]"
cmd.exe /c "C:\\Users\\santi\\repos\\rubens-ares\\build.bat" 2>&1 | tail -2
cp /c/Users/santi/repos/rubens-ares/build/web-ui/out/ares /c/Users/santi/repos/rubens-ares/web/ares.js
cp /c/Users/santi/repos/rubens-ares/build/web-ui/out/ares.wasm /c/Users/santi/repos/rubens-ares/web/ares.wasm
echo "[build] deployed"

# Kill any existing server on port 3000
EXISTING=$(cmd.exe /c "netstat -ano | findstr LISTENING | findstr :3000" 2>&1 | grep -oP '\d+\s*$' | head -1)
if [ -n "$EXISTING" ]; then
  cmd.exe /c "taskkill /PID $EXISTING /F" >/dev/null 2>&1
  sleep 1
fi

# Start server (foreground, in subshell, with timeout)
(
  cd /c/Users/santi/repos/rubens-ares/web
  timeout $((DURATION + 10)) node profile-server.js --duration=$DURATION --label="$LABEL"
) &
SERVER_PID=$!
sleep 2

# Tell connected client to reload (it's the previous tab still open)
curl -s -X POST http://localhost:3000/reload >/dev/null
echo "[reload] sent"

# Wait for server to finish
wait $SERVER_PID
echo "[done] $LABEL"
