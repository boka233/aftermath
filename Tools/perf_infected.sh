#!/usr/bin/env bash
# Stage 4 load test: 1 dedicated server with N extra infected (-AftermathInfected=N) + 1 client standing
# in the middle of them, so every infected is within the "near" update band (worst case).
# The player is made invulnerable (-AftermathAllowDebug -AftermathGodMode, dev builds only) so the horde
# keeps chasing it for the whole run instead of killing it in the first seconds.
# Reads the server's periodic "InfectedStats" lines and checks the server keeps 30 fps.
# Usage: Tools/perf_infected.sh [packaged|editor] [count] [seconds]
set -u

MODE="${1:-packaged}"
COUNT="${2:-150}"
SECONDS_RUN="${3:-45}"
# Own port so a manually started game on 7777 is never hit by (or confused with) the test.
PORT="${AFTERMATH_TEST_PORT:-17777}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT_W="$(cygpath -w "$ROOT/Aftermath.uproject")"
EDITOR_BIN="/d/UE/UE_5.8.3/Engine/Binaries/Win64/UnrealEditor.exe"
LOGDIR="$ROOT/Saved/PerfInfected"
mkdir -p "$LOGDIR"
rm -f "$LOGDIR"/*.log
SERVER_LOG="$LOGDIR/server.log"
CLIENT_LOG="$LOGDIR/client.log"

SERVER_PID=""
CLIENT_PID=""
stop_pid() {
	local pid="$1" winpid=""
	[ -z "$pid" ] && return
	[ -r "/proc/$pid/winpid" ] && winpid="$(cat "/proc/$pid/winpid" 2>/dev/null)"
	[ -n "$winpid" ] && taskkill //F //T //PID "$winpid" >/dev/null 2>&1
	kill "$pid" 2>/dev/null || true
}
cleanup() { stop_pid "$CLIENT_PID"; stop_pid "$SERVER_PID"; }
trap cleanup EXIT

if [ "$MODE" = "editor" ]; then
	"$EDITOR_BIN" "$PROJECT_W" -server -log -unattended -nosplash -nullrhi -port=$PORT -AftermathInfected=$COUNT \
		-AftermathAllowDebug -AftermathGodMode -abslog="$(cygpath -w "$SERVER_LOG")" >/dev/null 2>&1 &
else
	"$ROOT/Packaged/WindowsServer/AftermathServer.exe" -log -unattended -port=$PORT -AftermathInfected=$COUNT \
		-AftermathAllowDebug -AftermathGodMode -abslog="$(cygpath -w "$SERVER_LOG")" >/dev/null 2>&1 &
fi
SERVER_PID=$!

for _ in $(seq 1 240); do
	grep -q "listening on port" "$SERVER_LOG" 2>/dev/null && break
	sleep 1
done
grep -q "listening on port" "$SERVER_LOG" 2>/dev/null || { echo "FAIL: server not listening"; exit 1; }
echo "server listening ($MODE, infected=$COUNT)"

# Client without rendering: it only has to exist as a player the infected react to.
if [ "$MODE" = "editor" ]; then
	"$EDITOR_BIN" "$PROJECT_W" "127.0.0.1:$PORT#StartA" -game -log -unattended -nosplash -nullrhi \
		-abslog="$(cygpath -w "$CLIENT_LOG")" >/dev/null 2>&1 &
else
	"$ROOT/Packaged/WindowsClient/AftermathClient.exe" "127.0.0.1:$PORT#StartA" -log -unattended -nosplash -nullrhi \
		-abslog="$(cygpath -w "$CLIENT_LOG")" >/dev/null 2>&1 &
fi
CLIENT_PID=$!

for _ in $(seq 1 120); do
	grep -q "PlayerJoined" "$SERVER_LOG" 2>/dev/null && break
	sleep 1
done
sleep "$SECONDS_RUN"

echo "--- server stats (every 5 s)"
grep -E "InfectedLoadSpawn|InfectedStats" "$SERVER_LOG" | sed -E 's/^.*\[Aftermath\] //' | tail -12

# Judge only the samples taken while the player was in the game and the horde was awake.
STATS=$(grep -E "InfectedStats .*players=1" "$SERVER_LOG" | sed -E 's/^.*\[Aftermath\] //')
N=$(printf '%s\n' "$STATS" | grep -c "InfectedStats" || true)
if [ "${N:-0}" -lt 3 ]; then
	echo "FAIL: only ${N:-0} stats samples with a player present"
	exit 1
fi
SPAWNED=$(grep -oE "InfectedLoadSpawn requested=[0-9]+ spawned=[0-9]+ navmesh=[01]" "$SERVER_LOG" | tail -1)
MINFPS=$(printf '%s\n' "$STATS" | grep -oE "fps=[0-9.]+" | cut -d= -f2 | sort -n | head -1)
AVGMS=$(printf '%s\n' "$STATS" | grep -oE "world_tick_ms_avg=[0-9.]+" | cut -d= -f2 | awk '{s+=$1} END {printf "%.2f", s/NR}')
MAXMS=$(printf '%s\n' "$STATS" | grep -oE "world_tick_ms_max=[0-9.]+" | cut -d= -f2 | sort -n | tail -1)
MGRMS=$(printf '%s\n' "$STATS" | grep -oE "manager_ms_avg=[0-9.]+" | cut -d= -f2 | awk '{s+=$1} END {printf "%.3f", s/NR}')
AWAKE=$(printf '%s\n' "$STATS" | grep -oE "awake=[0-9]+" | cut -d= -f2 | sort -n | tail -1)
CHASING=$(printf '%s\n' "$STATS" | grep -oE "chasing=[0-9]+" | cut -d= -f2 | sort -n | tail -1)
echo "--- summary: $SPAWNED samples=$N min_fps=$MINFPS world_tick_ms_avg=$AVGMS world_tick_ms_max=$MAXMS manager_ms_avg=$MGRMS max_awake=$AWAKE max_chasing=$CHASING"

grep -qE "Ensure condition failed|Assertion failed|Fatal error|Unhandled Exception" "$SERVER_LOG" && { echo "FAIL: server crashed or ensured"; exit 1; }
# 30 Hz server: every 5 s window should hold ~150 frames; allow a little slack for timer jitter.
awk -v f="$MINFPS" 'BEGIN { exit !(f >= 28.5) }' && echo "RESULT: PASS (server holds 30 fps)" || { echo "RESULT: FAIL (min fps $MINFPS < 28.5)"; exit 1; }
