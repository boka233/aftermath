#!/usr/bin/env bash
# Stage 3 multiplayer combat test: 1 dedicated server + 2 clients (shooter at StartE, target at StartF).
# Usage: Tools/smoke_stage3.sh [packaged|editor] [timeout_seconds]
set -u

MODE="${1:-packaged}"
TIMEOUT="${2:-240}"
PORT=7777

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT_W="$(cygpath -w "$ROOT/Aftermath.uproject")"
EDITOR_BIN="/d/UE/UE_5.8.3/Engine/Binaries/Win64/UnrealEditor.exe"
LOGDIR="$ROOT/Saved/SmokeStage3"
mkdir -p "$LOGDIR"
rm -f "$LOGDIR"/*.log
SERVER_LOG="$LOGDIR/server.log"
SHOOTER_LOG="$LOGDIR/shooter.log"
TARGET_LOG="$LOGDIR/target.log"

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

# -AftermathAllowDebug lets the test script request loaded magazines (dev-only RPC, compiled out of Shipping).
start_server() {
	if [ "$MODE" = "editor" ]; then
		"$EDITOR_BIN" "$PROJECT_W" -server -log -unattended -nosplash -nullrhi -port=$PORT -AftermathAllowDebug \
			-abslog="$(cygpath -w "$SERVER_LOG")" >/dev/null 2>&1 &
	else
		"$ROOT/Packaged/WindowsServer/AftermathServer.exe" -log -unattended -port=$PORT -AftermathAllowDebug \
			-abslog="$(cygpath -w "$SERVER_LOG")" >/dev/null 2>&1 &
	fi
	SERVER_PID=$!
}

# $1 = role, $2 = portal (PlayerStart tag), $3 = log file
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

start_server
for _ in $(seq 1 "$TIMEOUT"); do
	grep -q "listening on port" "$SERVER_LOG" 2>/dev/null && break
	sleep 1
done
grep -q "listening on port" "$SERVER_LOG" 2>/dev/null || { echo "FAIL: server not listening"; tail -20 "$SERVER_LOG" | cut -c1-200; exit 1; }
echo "server listening ($MODE)"

# Target first so it is standing at StartF before the shooter starts firing.
start_client target StartF "$TARGET_LOG"
sleep 3
start_client shooter StartE "$SHOOTER_LOG"

for _ in $(seq 1 "$TIMEOUT"); do
	a=$(grep -c "role=shooter step=done" "$SHOOTER_LOG" 2>/dev/null || true)
	b=$(grep -c "role=target step=done" "$TARGET_LOG" 2>/dev/null || true)
	[ "${a:-0}" -ge 1 ] && [ "${b:-0}" -ge 1 ] && break
	sleep 1
done
sleep 2

PASS=0
FAIL=0
check() {
	# $1 = description, $2 = file, $3 = extended regex
	if grep -qE "$3" "$2" 2>/dev/null; then
		echo "  ok   $1"; PASS=$((PASS + 1))
	else
		echo "  FAIL $1"; FAIL=$((FAIL + 1))
	fi
}
check_absent() {
	if grep -qE "$3" "$2" 2>/dev/null; then
		echo "  FAIL $1"; FAIL=$((FAIL + 1))
	else
		echo "  ok   $1"; PASS=$((PASS + 1))
	fi
}

echo "--- connection"
check "2 players joined"                  "$SERVER_LOG"  "PlayerJoined .*players=2"
check "shooter script ran"                "$SHOOTER_LOG" "role=shooter step=done"
check "target script ran"                 "$TARGET_LOG"  "role=target step=done"

echo "--- arming"
check "shooter found the pistol"          "$SHOOTER_LOG" "step=take_pistol found=1"
check "target found the vest"             "$TARGET_LOG"  "step=wear_vest found=1"
check "server gave test magazines"        "$SERVER_LOG"  "DebugGive item=[Mm][Aa][Gg]9[Mm][Mm] rounds=15"
check "magazine loaded into pistol"       "$SHOOTER_LOG" "role=shooter t=[0-9.]+ .*rounds=1[0-5]"

echo "--- shooting (server authoritative)"
check "server accepted a shot"            "$SERVER_LOG"  "\] Shot by=.* weapon=[Pp][Ii][Ss][Tt][Oo][Ll]"
check "a shot hit the target"             "$SERVER_LOG"  "\] Shot by=.* hit=AftermathCharacter[^ ]* zone=(head|torso|arms|legs)"
check "lag compensation used"             "$SERVER_LOG"  "\] Shot by=.* rewind_ms=[0-9]"
check "hit applied damage"                "$SERVER_LOG"  "\] Hit victim=.* damage=[1-9]"
check "vest reduced a torso hit"          "$SERVER_LOG"  "\] Hit victim=.* armor=[Pp][Ll][Aa][Tt][Ee]"
check "cheated origin rejected"           "$SERVER_LOG"  "ShotRejected reason=bad_origin"

echo "--- reload"
check "reload completed on server"        "$SERVER_LOG"  "\] Reload by=.* result=(swapped|reloaded|ok)[^ ]* rounds=1[0-5]"
check "shooter sees full mag after R"     "$SHOOTER_LOG" "step=after_reload rounds=1[0-5]"

echo "--- kill"
check "server recorded the kill"          "$SERVER_LOG"  "\] Kill killer=.* victim="
check "target saw itself die"             "$TARGET_LOG"  "role=target step=observed_own_death"
check "server logged the death"           "$SERVER_LOG"  "PlayerDied who=.* cause=damage"
# The script component starts once per possessed body, so a respawn shows up as a second "started".
starts=$(grep -c "role=target started" "$TARGET_LOG" 2>/dev/null || true)
if [ "${starts:-0}" -ge 2 ]; then echo "  ok   target respawned (starts=$starts)"; PASS=$((PASS + 1))
else echo "  FAIL target respawned (starts=${starts:-0})"; FAIL=$((FAIL + 1)); fi

echo "--- client feedback (replicated)"
check "shooter got a hit marker"          "$SHOOTER_LOG" "hitmarker_age=0\.[0-9]"
check "target got damage indicator"       "$TARGET_LOG"  "dmg_indicator_age=0\.[0-9]"
check "target lost health"                "$TARGET_LOG"  "role=target t=[0-9.]+ .* hp=([0-9]|[1-8][0-9]|9[0-9])( |\.)"
check "target saw shooter's tracers"      "$TARGET_LOG"  "other_shots_seen=[1-9]"

echo "--- health"
for f in "$SERVER_LOG" "$SHOOTER_LOG" "$TARGET_LOG"; do
	n=$(basename "$f" .log)
	check_absent "$n: no ensure/assert/crash" "$f" "Ensure condition failed|Assertion failed|Fatal error|Unhandled Exception"
	check_absent "$n: no late-module Iris warning" "$f" "BroadcastLoadedModulesUpdated\(\) called while there are [1-9]"
	check_absent "$n: item database loaded" "$f" "ItemDatabase: cannot read"
done

if [ "$FAIL" -gt 0 ]; then
	echo "--- last observations"
	grep -E "role=shooter (t=|step=)" "$SHOOTER_LOG" | tail -8 | cut -c30-300
	grep -E "role=target (t=|step=)" "$TARGET_LOG" | tail -6 | cut -c30-300
	grep -E "\[Aftermath\]" "$SERVER_LOG" | cut -c30-240 | tail -16
fi

echo "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
