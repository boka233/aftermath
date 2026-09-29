#!/usr/bin/env bash
# Stage-0 smoke test: start the dedicated server, connect N clients, and check the
# server log for the "[Aftermath] PlayerJoined" marker written by AAftermathGameMode::PostLogin.
#
# Usage: Tools/smoke_connect.sh [clients=1] [timeout_sec=180]
# Requires: Binaries/Win64/AftermathServer.exe and AftermathClient.exe (uncooked, run from project).
set -u

CLIENTS="${1:-1}"
TIMEOUT="${2:-180}"
PORT=7777

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT="$(cygpath -w "$ROOT/Aftermath.uproject")"
EDITOR_BIN="/d/UE/UE_5.8.3/Engine/Binaries/Win64/UnrealEditor.exe"
LOGDIR="$ROOT/Saved/Smoke"
mkdir -p "$LOGDIR"
SERVER_LOG="$LOGDIR/server.log"
rm -f "$LOGDIR"/*.log

# Uncooked run through the editor binary: -server / -game select the role.
"$EDITOR_BIN" "$PROJECT" -server -log -unattended -nosplash -nullrhi -port=$PORT \
	-abslog="$(cygpath -w "$SERVER_LOG")" >/dev/null 2>&1 &
SERVER_PID=$!
CLIENT_PIDS=()

# Stop only the processes this script launched. Never kill by image name:
# the user may have their own UnrealEditor.exe open with unsaved work.
stop_pid() {
	local pid="$1" winpid=""
	[ -z "$pid" ] && return
	[ -r "/proc/$pid/winpid" ] && winpid="$(cat "/proc/$pid/winpid" 2>/dev/null)"
	if [ -n "$winpid" ]; then
		taskkill //F //T //PID "$winpid" >/dev/null 2>&1 || true
	fi
	kill "$pid" 2>/dev/null || true
}

cleanup() {
	for pid in "${CLIENT_PIDS[@]}" "$SERVER_PID"; do
		stop_pid "$pid"
	done
}
trap cleanup EXIT

# Wait until the server is listening.
for _ in $(seq 1 "$TIMEOUT"); do
	grep -q "listening on port\|IpNetDriver listening\|GameNetDriver IpNetDriver" "$SERVER_LOG" 2>/dev/null && break
	sleep 1
done
if ! grep -q "listening on port\|IpNetDriver listening\|GameNetDriver IpNetDriver" "$SERVER_LOG" 2>/dev/null; then
	echo "FAIL: server did not start listening within ${TIMEOUT}s"
	tail -40 "$SERVER_LOG" 2>/dev/null
	exit 1
fi
echo "server listening on $PORT"

for i in $(seq 1 "$CLIENTS"); do
	"$EDITOR_BIN" "$PROJECT" 127.0.0.1:$PORT -game -log -unattended -nosplash -windowed -ResX=640 -ResY=360 \
		-abslog="$(cygpath -w "$LOGDIR/client$i.log")" >/dev/null 2>&1 &
	CLIENT_PIDS+=($!)
done

for _ in $(seq 1 "$TIMEOUT"); do
	JOINED=$(grep -c "\[Aftermath\] PlayerJoined" "$SERVER_LOG" 2>/dev/null || true)
	[ "${JOINED:-0}" -ge "$CLIENTS" ] && break
	sleep 1
done

JOINED=$(grep -c "\[Aftermath\] PlayerJoined" "$SERVER_LOG" 2>/dev/null || true)
grep "\[Aftermath\]\|Iris\|ReplicationSystem" "$SERVER_LOG" | head -20
if [ "${JOINED:-0}" -ge "$CLIENTS" ]; then
	echo "PASS: $JOINED/$CLIENTS clients joined"
	exit 0
fi
echo "FAIL: only ${JOINED:-0}/$CLIENTS clients joined"
tail -30 "$LOGDIR/client1.log" 2>/dev/null
exit 1
