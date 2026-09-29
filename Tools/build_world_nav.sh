#!/usr/bin/env bash
# Bake the World map navmesh into World Partition navigation data chunks (run after build_world_map.py).
# Usage: Tools/build_world_nav.sh
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT_W="$(cygpath -w "$ROOT/Aftermath.uproject")"
CMD_BIN="/d/UE/UE_5.8.3/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
LOG="$ROOT/Saved/Logs/build_world_nav.log"
# Git Bash would turn "/Game/..." into a Windows path.
export MSYS2_ARG_CONV_EXCL="/Game"

START=$(date +%s)
# Note: -CleanPackages only deletes existing chunks and builds nothing. build_world_map.py already
# regenerates the map (and its external actors) from scratch, so a plain run rebuilds everything.
"$CMD_BIN" "$PROJECT_W" "/Game/Maps/World" -run=WorldPartitionBuilderCommandlet -Builder=WorldPartitionNavigationDataBuilder \
	-unattended -nosplash -RenderOffscreen -AllowCommandletRendering -nop4 -abslog="$(cygpath -w "$LOG")" >/dev/null 2>&1
EXIT=$?
END=$(date +%s)

CHUNKS=$(grep -rl "NavigationDataChunkActor" "$ROOT/Content/__ExternalActors__/Maps/World" 2>/dev/null | wc -l)
ITER=$(grep -cE "GenerateNavigationData for LoadedBounds" "$LOG")
SIZE=$(du -sh "$ROOT/Content/__ExternalActors__/Maps/World" 2>/dev/null | cut -f1)
echo "exit=$EXIT seconds=$((END - START)) iterations=$ITER nav_chunk_packages=$CHUNKS world_external_actors=$SIZE"
grep -E "LogWorldPartitionNavigationDataBuilder|Error:|Ensure condition|Assertion failed" "$LOG" \
	| grep -vE "UnifiedErrorTest|GameFeatures|LogPython|FError" | cut -c30-260 | tail -12
[ "$EXIT" -eq 0 ] && [ "$CHUNKS" -gt 0 ]
