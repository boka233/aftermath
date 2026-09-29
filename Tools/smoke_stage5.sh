#!/usr/bin/env bash
# Stage 5 multiplayer test on the 10 x 10 km World map: 1 dedicated server + 2 scripted clients.
#  - explorer: coastal spawn (#Coast), walks inland (World Partition streaming), reports clock/weather
#  - looter:   city spawn (#City), takes the nearest item the loot economy placed
# Server runs at 21:00 in light rain so the clock, daylight and rain replicate with non-default values,
# with 60 extra infected (load) and the default loot economy.
# Usage: Tools/smoke_stage5.sh [packaged|editor] [timeout_seconds]
set -u

MODE="${1:-packaged}"
TIMEOUT="${2:-300}"
PORT="${AFTERMATH_TEST_PORT:-17777}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT_W="$(cygpath -w "$ROOT/Aftermath.uproject")"
EDITOR_BIN="/d/UE/UE_5.8.3/Engine/Binaries/Win64/UnrealEditor.exe"
LOGDIR="$ROOT/Saved/SmokeStage5"
mkdir -p "$LOGDIR"
rm -f "$LOGDIR"/*.log
SERVER_LOG="$LOGDIR/server.log"
EXPLORER_LOG="$LOGDIR/explorer.log"
LOOTER_LOG="$LOGDIR/looter.log"
# Git Bash would turn "/Game/..." into a Windows path.
export MSYS2_ARG_CONV_EXCL="/Game"

SERVER_PID=""
CLIENT_PIDS=()
stop_pid() {
	local pid="$1" winpid=""
	[ -z "$pid" ] && return
	[ -r "/proc/$pid/winpid" ] && winpid="$(cat "/proc/$pid/winpid" 2>/dev/null)"
	[ -n "$winpid" ] && taskkill //F //T //PID "$winpid" >/dev/null 2>&1
	kill "$pid" 2>/dev/null || true
}
cleanup() { for pid in "${CLIENT_PIDS[@]}" "$SERVER_PID"; do stop_pid "$pid"; done; }
trap cleanup EXIT

SERVER_ARGS=(/Game/Maps/World -log -unattended -port=$PORT -AftermathAllowDebug -AftermathInfected=60 -AftermathHour=21 -AftermathRain=0.4)
if [ "$MODE" = "editor" ]; then
	"$EDITOR_BIN" "$PROJECT_W" "${SERVER_ARGS[@]}" -server -nosplash -nullrhi -abslog="$(cygpath -w "$SERVER_LOG")" >/dev/null 2>&1 &
else
	"$ROOT/Packaged/WindowsServer/AftermathServer.exe" "${SERVER_ARGS[@]}" -abslog="$(cygpath -w "$SERVER_LOG")" >/dev/null 2>&1 &
fi
SERVER_PID=$!

for _ in $(seq 1 "$TIMEOUT"); do
	grep -q "listening on port" "$SERVER_LOG" 2>/dev/null && break
	sleep 1
done
grep -q "listening on port" "$SERVER_LOG" 2>/dev/null || { echo "FAIL: server not listening"; tail -20 "$SERVER_LOG" | cut -c1-200; exit 1; }
echo "server listening ($MODE)"
# Let the first loot pass run before the looter arrives.
for _ in $(seq 1 120); do
	grep -q "LootPass n=1" "$SERVER_LOG" 2>/dev/null && break
	sleep 1
done

# $1 = role, $2 = portal, $3 = log
start_client() {
	local url="127.0.0.1:$PORT#$2"
	if [ "$MODE" = "editor" ]; then
		"$EDITOR_BIN" "$PROJECT_W" "$url" -game -log -unattended -nosplash -windowed -ResX=640 -ResY=360 \
			-AftermathScript="$1" -abslog="$(cygpath -w "$3")" >/dev/null 2>&1 &
	else
		"$ROOT/Packaged/WindowsClient/AftermathClient.exe" "$url" -log -unattended -nosplash -windowed -ResX=640 -ResY=360 \
			-AftermathScript="$1" -abslog="$(cygpath -w "$3")" >/dev/null 2>&1 &
	fi
	CLIENT_PIDS+=($!)
}
start_client explorer Coast "$EXPLORER_LOG"
start_client looter City "$LOOTER_LOG"

for _ in $(seq 1 "$TIMEOUT"); do
	a=$(grep -c "role=explorer step=done" "$EXPLORER_LOG" 2>/dev/null || true)
	b=$(grep -c "role=looter step=done" "$LOOTER_LOG" 2>/dev/null || true)
	[ "${a:-0}" -ge 1 ] && [ "${b:-0}" -ge 1 ] && break
	sleep 1
done
sleep 6

PASS=0
FAIL=0
check() {
	if grep -qE "$3" "$2" 2>/dev/null; then echo "  ok   $1"; PASS=$((PASS + 1)); else echo "  FAIL $1"; FAIL=$((FAIL + 1)); fi
}
check_absent() {
	if grep -qE "$3" "$2" 2>/dev/null; then echo "  FAIL $1"; FAIL=$((FAIL + 1)); else echo "  ok   $1"; PASS=$((PASS + 1)); fi
}

echo "--- world"
check "server loaded the World map"        "$SERVER_LOG"   "LoadMap: .*/Game/Maps/World"
check "2 players joined"                   "$SERVER_LOG"   "PlayerJoined .*players=2"
check "explorer spawned on the coast"      "$SERVER_LOG"   "PlayerSpawned pawn=.* at=V\(X=-?[0-9]"
check "explorer script ran"                "$EXPLORER_LOG" "role=explorer step=done"
check "looter script ran"                  "$LOOTER_LOG"   "role=looter step=done"
check "explorer stands on terrain"         "$EXPLORER_LOG" "step=look_around z=[1-9][0-9]{1,4} "
check "explorer walked inland"             "$EXPLORER_LOG" "step=after_walk "
check_absent "explorer never fell through" "$EXPLORER_LOG" "self_z=-[0-9]{4,}"

echo "--- day/night + weather (server authoritative, replicated)"
check "server clock started at 21:00"      "$SERVER_LOG"   "WorldClock start_hour=21.00 .* rain=0.40"
check "client sees the evening hour"       "$EXPLORER_LOG" "hour=21\.[0-9]{2} "
check "client sees dusk light (<1)"        "$EXPLORER_LOG" "daylight=0\.[0-9]{2} "
check "client sees the rain"               "$EXPLORER_LOG" "rain=0\.40"

echo "--- loot economy"
check "loot config loaded"                 "$SERVER_LOG"   "LootConfig loaded items=[1-9]"
check "economy spawned loot"               "$SERVER_LOG"   "LootPass n=1 points=[1-9][0-9]+ .* spawned=[1-9]"
check "looter saw loot nearby"             "$LOOTER_LOG"   "step=take_nearest_loot seen=[1-9][0-9]* nearest=[0-9]+ item=[A-Za-z]"
check "server gave the looter the item"    "$SERVER_LOG"   "ItemPicked item=[A-Za-z0-9]+ count=[0-9]+ by="

echo "--- infected + navigation"
check "infected on the World map"          "$SERVER_LOG"   "InfectedLoadSpawn requested=60 spawned=[1-9][0-9] navmesh=1"
check "infected pathfind on the navmesh"   "$SERVER_LOG"   "InfectedStats .*nav_paths=[1-9]"
check_absent "no straight-line fallback"   "$SERVER_LOG"   "InfectedStats .*direct_paths=[1-9]"

echo "--- health"
for f in "$SERVER_LOG" "$EXPLORER_LOG" "$LOOTER_LOG"; do
	n=$(basename "$f" .log)
	check_absent "$n: no ensure/assert/crash" "$f" "Ensure condition failed|Assertion failed|Fatal error|Unhandled Exception"
	check_absent "$n: no late-module Iris warning" "$f" "BroadcastLoadedModulesUpdated\(\) called while there are [1-9]"
done

echo "--- server performance (every 5 s)"
grep -E "InfectedStats .*players=2" "$SERVER_LOG" | sed -E 's/^.*\[Aftermath\] //' | grep -oE "fps=[0-9.]+|world_tick_ms_avg=[0-9.]+|world_tick_ms_max=[0-9.]+|chasing=[0-9]+" | paste -d' ' - - - - | tail -4
MINFPS=$(grep -E "InfectedStats .*players=2" "$SERVER_LOG" | grep -oE "fps=[0-9.]+" | cut -d= -f2 | sort -n | head -1)
if awk -v f="${MINFPS:-0}" 'BEGIN { exit !(f >= 28.5) }'; then echo "  ok   server holds 30 fps (min $MINFPS)"; PASS=$((PASS + 1)); else echo "  FAIL server fps (min ${MINFPS:-none})"; FAIL=$((FAIL + 1)); fi
SRV_MB=$(grep -oE "Process Physical Memory: [0-9.]+ MB used" "$SERVER_LOG" | tail -1)
echo "  server memory: ${SRV_MB:-not reported}"

if [ "$FAIL" -gt 0 ]; then
	echo "--- last observations"
	grep -E "role=explorer (t=|step=)" "$EXPLORER_LOG" | tail -4 | cut -c30-320
	grep -E "role=looter (t=|step=)" "$LOOTER_LOG" | tail -4 | cut -c30-320
	grep -E "\[Aftermath\] (Loot|WorldClock|PlayerSpawned|ItemPicked|InventoryRejected|DebugTeleport)" "$SERVER_LOG" | cut -c30-240 | tail -12
fi

echo "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
