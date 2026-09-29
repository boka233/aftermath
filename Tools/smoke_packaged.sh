#!/usr/bin/env bash
# Packaged smoke test: cooked AftermathServer.exe + N cooked AftermathClient.exe on 127.0.0.1.
# Passes when the server log shows N "[Aftermath] PlayerJoined" lines and no ensures/fatal errors.
#
# Usage: Tools/smoke_packaged.sh [clients=2] [timeout_sec=180]
# Build first with RunUAT BuildCookRun ... -archivedirectory=<project>/Packaged
set -u

CLIENTS="${1:-2}"
TIMEOUT="${2:-180}"
PORT=7777

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SERVER_EXE="$ROOT/Packaged/WindowsServer/AftermathServer.exe"
CLIENT_EXE="$ROOT/Packaged/WindowsClient/AftermathClient.exe"
LOGDIR="$ROOT/Saved/SmokePackaged"
mkdir -p "$LOGDIR"
rm -f "$LOGDIR"/*.log
SERVER_LOG="$LOGDIR/server.log"

for exe in "$SERVER_EXE" "$CLIENT_EXE"; do
	[ -x "$exe" ] || { echo "FAIL: missing $exe (package first)"; exit 1; }
done

SERVER_PID=""
CLIENT_PIDS=()

# Stop only processes this script launched (never kill by image name).
stop_pid() {
	local pid="$1" winpid=""
	[ -z "$pid" ] && return
	[ -r "/proc/$pid/winpid" ] && winpid="$(cat "/proc/$pid/winpid" 2>/dev/null)"
	[ -n "$winpid" ] && taskkill //F //T //PID "$winpid" >/dev/null 2>&1
	kill "$pid" 2>/dev/null || true
}
cleanup() {
	for pid in "${CLIENT_PIDS[@]}" "$SERVER_PID"; do stop_pid "$pid"; done
}
trap cleanup EXIT

"$SERVER_EXE" -log -unattended -port=$PORT -abslog="$(cygpath -w "$SERVER_LOG")" >/dev/null 2>&1 &
SERVER_PID=$!

for _ in $(seq 1 "$TIMEOUT"); do
	grep -q "listening on port" "$SERVER_LOG" 2>/dev/null && break
	sleep 1
done
if ! grep -q "listening on port" "$SERVER_LOG" 2>/dev/null; then
	echo "FAIL: server did not listen within ${TIMEOUT}s"
	tail -30 "$SERVER_LOG" 2>/dev/null | cut -c1-220
	exit 1
fi
echo "server listening on $PORT"

for i in $(seq 1 "$CLIENTS"); do
	"$CLIENT_EXE" 127.0.0.1:$PORT -log -unattended -nosplash -windowed -ResX=640 -ResY=360 \
		-abslog="$(cygpath -w "$LOGDIR/client$i.log")" >/dev/null 2>&1 &
	CLIENT_PIDS+=($!)
done

for _ in $(seq 1 "$TIMEOUT"); do
	JOINED=$(grep -c "\[Aftermath\] PlayerJoined" "$SERVER_LOG" 2>/dev/null || true)
	[ "${JOINED:-0}" -ge "$CLIENTS" ] && break
	sleep 1
done
# Let replication run briefly so late ensures/crashes surface before we judge.
sleep 10

JOINED=$(grep -c "\[Aftermath\] PlayerJoined" "$SERVER_LOG" 2>/dev/null || true)
FAILED=0
for f in "$LOGDIR"/*.log; do
	n=$(basename "$f" .log)
	ens=$(grep -c "Ensure condition failed" "$f" 2>/dev/null || true)
	fatal=$(grep -cE "Fatal error|Assertion failed|Unhandled Exception" "$f" 2>/dev/null || true)
	iris=$(grep -c "ReplicationSystem.*is created" "$f" 2>/dev/null || true)
	echo "$n: iris=$iris ensures=$ens fatal=$fatal"
	[ "${ens:-0}" -gt 0 ] || [ "${fatal:-0}" -gt 0 ] && FAILED=1
done

if [ "${JOINED:-0}" -ge "$CLIENTS" ] && [ "$FAILED" -eq 0 ]; then
	echo "PASS: $JOINED/$CLIENTS clients joined packaged server"
	exit 0
fi
echo "FAIL: joined=${JOINED:-0}/$CLIENTS errors=$FAILED"
grep -hE "Ensure condition failed|Fatal error|Assertion failed" "$LOGDIR"/*.log | cut -c1-220 | sort -u | head
exit 1
