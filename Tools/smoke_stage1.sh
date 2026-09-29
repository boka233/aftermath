#!/usr/bin/env bash
# Stage-1 network test: 1 dedicated server + 2 scripted clients on /Game/Maps/Proto.
#
#   mover    (spawns at StartA): crouch -> prone -> stand, walks to HouseB, opens its door with F,
#                                walks in and takes the water bottle with F
#   observer (spawns at StartC): asks the server for a far-away pickup (must be refused), vaults a 60cm wall
#
# Each client logs "[AftermathTest] role=... other_stance=..." observations of the other player, so we can
# check that stance changes replicate to simulated proxies. The server logs authoritative outcomes.
#
# Usage: Tools/smoke_stage1.sh [packaged|editor] [timeout_sec=240]
set -u

MODE="${1:-packaged}"
TIMEOUT="${2:-240}"
PORT=7777

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT_W="$(cygpath -w "$ROOT/Aftermath.uproject")"
EDITOR_BIN="/d/UE/UE_5.8.3/Engine/Binaries/Win64/UnrealEditor.exe"
LOGDIR="$ROOT/Saved/SmokeStage1"
mkdir -p "$LOGDIR"
rm -f "$LOGDIR"/*.log
SERVER_LOG="$LOGDIR/server.log"
MOVER_LOG="$LOGDIR/mover.log"
OBSERVER_LOG="$LOGDIR/observer.log"

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

start_server() {
	if [ "$MODE" = "editor" ]; then
		"$EDITOR_BIN" "$PROJECT_W" -server -log -unattended -nosplash -nullrhi -port=$PORT \
			-abslog="$(cygpath -w "$SERVER_LOG")" >/dev/null 2>&1 &
	else
		"$ROOT/Packaged/WindowsServer/AftermathServer.exe" -log -unattended -port=$PORT \
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

start_client mover StartA "$MOVER_LOG"
start_client observer StartC "$OBSERVER_LOG"

# Scripts finish at t=30s after each client starts ticking; wait for both "step=done" or timeout.
for _ in $(seq 1 "$TIMEOUT"); do
	a=$(grep -c "role=mover step=done" "$MOVER_LOG" 2>/dev/null || true)
	b=$(grep -c "role=observer step=done" "$OBSERVER_LOG" 2>/dev/null || true)
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
check "2 players joined"                "$SERVER_LOG"   "PlayerJoined .*players=2"
check "mover script ran"                "$MOVER_LOG"    "role=mover step=done"
check "observer script ran"             "$OBSERVER_LOG" "role=observer step=done"

echo "--- stance (predicted locally)"
check "mover sees itself crouched"      "$MOVER_LOG"    "role=mover t=[0-9.]+ self_stance=Crouched"
check "mover sees itself prone"         "$MOVER_LOG"    "role=mover t=[0-9.]+ self_stance=Prone"

echo "--- stance (replicated to the other client)"
check "observer sees mover crouched"    "$OBSERVER_LOG" "other_stance=Crouched other_half=60"
check "observer sees mover prone"       "$OBSERVER_LOG" "other_stance=Prone other_half=35"
check "observer sees mover standing"    "$OBSERVER_LOG" "t=(1[0-9]|2[0-9])\.[0-9] .*other_stance=Standing other_half=90"

echo "--- door (server authoritative, replicated)"
check "mover focused the door"          "$MOVER_LOG"    "step=interact_door focus=AftermathDoor"
check "server toggled the door"         "$SERVER_LOG"   "DoorToggled .*open=1"
check "mover sees the door open"        "$MOVER_LOG"    "door_open="
check "observer sees the door open"     "$OBSERVER_LOG" "door_open="

echo "--- pickup reach (server rejects a far request)"
check "observer sent far request"       "$OBSERVER_LOG" "step=cheat_far_pickup dist=[0-9]{4}"
check "server rejected it"              "$SERVER_LOG"   "InteractRejected reason=range"
# Actor names follow join order, so key off item ids: the only legit pickup is the mover's WaterBottle,
# so any other item leaving the world means the far (cheat) request got through.
check_absent "no item picked by cheat"  "$SERVER_LOG"   "ItemPicked item=(Bandage|CannedBeans|Knife)"

echo "--- pickup (legit, through focus + F)"
check "mover focused the loot"          "$MOVER_LOG"    "step=take_loot focus=AftermathPickup"
check "server gave the item"            "$SERVER_LOG"   "ItemPicked item=WaterBottle"
check "mover's inventory shows it"      "$MOVER_LOG"    "self_stance=[A-Za-z]+ self_z=[0-9.]+ items=1"
check "observer sees the pickup gone"   "$OBSERVER_LOG" "t=(2[5-9])\.[0-9] .*pickups=3"
check_absent "observer can't see mover's items" "$OBSERVER_LOG" "other_items="

echo "--- vault (predicted, validated on server)"
check "observer pressed vault"          "$OBSERVER_LOG" "step=press_vault"
# Height is measured from the capsule bottom, which floats ~2cm above the floor, so a 60cm wall reads 55-62.
check "server ran the vault"            "$SERVER_LOG"   "VaultStarted .*height=(5[5-9]|6[0-2])"
check "observer ended past the wall"    "$OBSERVER_LOG" "t=(2[0-9])\.[0-9] self_stance=Standing self_z=9[0-9]\."

echo "--- health"
for f in "$SERVER_LOG" "$MOVER_LOG" "$OBSERVER_LOG"; do
	n=$(basename "$f" .log)
	check_absent "$n: no ensure/assert/crash" "$f" "Ensure condition failed|Assertion failed|Fatal error|Unhandled Exception"
done

# Useful context on failure.
if [ "$FAIL" -gt 0 ]; then
	echo "--- last observations"
	grep "role=observer t=" "$OBSERVER_LOG" | tail -4 | cut -c30-260
	grep "role=mover t=" "$MOVER_LOG" | tail -4 | cut -c30-260
	grep -E "\[Aftermath\]" "$SERVER_LOG" | cut -c30-220 | tail -10
fi

echo "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
