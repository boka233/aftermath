#!/usr/bin/env bash
# Stage 4 multiplayer infected test: 1 dedicated server + 1 scripted client ("survivor" at StartG).
# The survivor makes noise, the lane civilian hears it, chases, attacks, and gets shot dead.
# Usage: Tools/smoke_stage4.sh [packaged|editor] [timeout_seconds]
set -u

MODE="${1:-packaged}"
TIMEOUT="${2:-240}"
# Own port so a manually started game on 7777 is never hit by (or confused with) the test.
PORT="${AFTERMATH_TEST_PORT:-17777}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT_W="$(cygpath -w "$ROOT/Aftermath.uproject")"
EDITOR_BIN="/d/UE/UE_5.8.3/Engine/Binaries/Win64/UnrealEditor.exe"
LOGDIR="$ROOT/Saved/SmokeStage4"
mkdir -p "$LOGDIR"
rm -f "$LOGDIR"/*.log
SERVER_LOG="$LOGDIR/server.log"
SURVIVOR_LOG="$LOGDIR/survivor.log"

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

# -AftermathAllowDebug lets the test script request a pistol and loaded magazines (dev-only, not in Shipping).
if [ "$MODE" = "editor" ]; then
	"$EDITOR_BIN" "$PROJECT_W" -server -log -unattended -nosplash -nullrhi -port=$PORT -AftermathAllowDebug \
		-abslog="$(cygpath -w "$SERVER_LOG")" >/dev/null 2>&1 &
else
	"$ROOT/Packaged/WindowsServer/AftermathServer.exe" -log -unattended -port=$PORT -AftermathAllowDebug \
		-abslog="$(cygpath -w "$SERVER_LOG")" >/dev/null 2>&1 &
fi
SERVER_PID=$!

for _ in $(seq 1 "$TIMEOUT"); do
	grep -q "listening on port" "$SERVER_LOG" 2>/dev/null && break
	sleep 1
done
grep -q "listening on port" "$SERVER_LOG" 2>/dev/null || { echo "FAIL: server not listening"; tail -20 "$SERVER_LOG" | cut -c1-200; exit 1; }
echo "server listening ($MODE)"

url="127.0.0.1:$PORT#StartG"
if [ "$MODE" = "editor" ]; then
	"$EDITOR_BIN" "$PROJECT_W" "$url" -game -log -unattended -nosplash -windowed -ResX=640 -ResY=360 \
		-AftermathScript=survivor -abslog="$(cygpath -w "$SURVIVOR_LOG")" >/dev/null 2>&1 &
else
	"$ROOT/Packaged/WindowsClient/AftermathClient.exe" "$url" -log -unattended -nosplash -windowed -ResX=640 -ResY=360 \
		-AftermathScript=survivor -abslog="$(cygpath -w "$SURVIVOR_LOG")" >/dev/null 2>&1 &
fi
CLIENT_PIDS+=($!)

for _ in $(seq 1 "$TIMEOUT"); do
	grep -q "role=survivor step=done" "$SURVIVOR_LOG" 2>/dev/null && break
	sleep 1
done
sleep 2

PASS=0
FAIL=0
check() {
	if grep -qE "$3" "$2" 2>/dev/null; then echo "  ok   $1"; PASS=$((PASS + 1)); else echo "  FAIL $1"; FAIL=$((FAIL + 1)); fi
}
check_absent() {
	if grep -qE "$3" "$2" 2>/dev/null; then echo "  FAIL $1"; FAIL=$((FAIL + 1)); else echo "  ok   $1"; PASS=$((PASS + 1)); fi
}

echo "--- world"
check "server built a navmesh"               "$SERVER_LOG"   "NavMeshReady data=.* bounds=V\(X=[1-9]"
check "placed infected spawned"              "$SERVER_LOG"   "InfectedSpawned who=.* type=civilian"
check "police and soldier types present"     "$SERVER_LOG"   "InfectedSpawned who=.* type=soldier"
check "survivor script ran"                  "$SURVIVOR_LOG" "role=survivor step=done"
check "client sees infected (replicated)"    "$SURVIVOR_LOG" "infected_seen=[1-9]"

echo "--- arming"
check "server gave the pistol"               "$SERVER_LOG"   "DebugGive item=Pistol"
check "magazine loaded"                      "$SURVIVOR_LOG" "role=survivor t=[0-9.]+ .*rounds=1[0-5]"

echo "--- infected behaviour (server)"
check "heard the gunshot and reacted"        "$SERVER_LOG"   "InfectedState who=.* from=idle to=(alert|chase) reason=gunshot"
check "started chasing the survivor"         "$SERVER_LOG"   "InfectedState who=.* to=chase .*target=AftermathCharacter"
check "reached and attacked"                 "$SERVER_LOG"   "InfectedState who=.* to=attack"
check "a swing landed"                       "$SERVER_LOG"   "InfectedAttack by=.* hit=1"
check "survivor took damage from it"         "$SERVER_LOG"   "\] Hit victim=AftermathCharacter.* by=AftermathInfected"
check "roar called others / logged"          "$SERVER_LOG"   "InfectedRoar by="
check "bullets hit the infected"             "$SERVER_LOG"   "InfectedHit victim=.* by=AftermathCharacter"
check "infected killed by the survivor"      "$SERVER_LOG"   "InfectedKilled victim=.* killer=AftermathCharacter"
check_absent "no infected opened a door"     "$SERVER_LOG"   "DoorToggled .*by=AftermathInfected"

echo "--- client view (replicated)"
check "client saw it chasing"                "$SURVIVOR_LOG" "inf_state=(chase|attack)"
check "client felt the hit"                  "$SURVIVOR_LOG" "role=survivor t=[0-9.]+ .* hp=([0-9]|[1-8][0-9]|9[0-9])( |\.)"
check "client got a hit marker"              "$SURVIVOR_LOG" "hitmarker_age=0\.[0-9]"
check "client saw it die"                    "$SURVIVOR_LOG" "inf_dead=1"

echo "--- health"
for f in "$SERVER_LOG" "$SURVIVOR_LOG"; do
	n=$(basename "$f" .log)
	check_absent "$n: no ensure/assert/crash" "$f" "Ensure condition failed|Assertion failed|Fatal error|Unhandled Exception"
	check_absent "$n: no late-module Iris warning" "$f" "BroadcastLoadedModulesUpdated\(\) called while there are [1-9]"
	check_absent "$n: item database loaded" "$f" "ItemDatabase: cannot read"
done

if [ "$FAIL" -gt 0 ]; then
	echo "--- last observations"
	grep -E "role=survivor (t=|step=)" "$SURVIVOR_LOG" | tail -8 | cut -c30-320
	grep -E "\[Aftermath\] (Infected|Hit|Shot|Kill)|LogNavigation" "$SERVER_LOG" | cut -c30-260 | tail -20
fi

echo "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
