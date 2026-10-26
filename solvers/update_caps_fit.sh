#!/usr/bin/env bash
# Regenerates the kFits + kJoints tables in assets/scenes/caps_fit.lua from a GLB.
# The bone list is hardcoded below; the probe fails on a name missing from the glb.
# Rebuild the solver after editing it:
#   cmake --build build -j --target bone_probe
set -euo pipefail
cd "$(dirname "$0")/.."

GLB="${1:-local/basegen8.glb}"     # relative to assets/
THRESHOLD="${2:-0.3}"              # skin-weight cutoff, matches the current kFits
LUA=assets/scenes/caps_fit.lua
BIN=build/bone_probe

BONES=(
    lShin rShin lThighBend rThighBend lThighTwist rThighTwist
    lShldrBend lShldrTwist lForearmBend lForearmTwist
    rShldrBend rShldrTwist rForearmBend rForearmTwist
    lHand lThumb1 lThumb2 lThumb3 lCarpal1 lCarpal2 lCarpal3 lCarpal4
    lIndex1 lIndex2 lIndex3 lMid1 lMid2 lMid3 lRing1 lRing2 lRing3
    lPinky1 lPinky2 lPinky3
    rHand rThumb1 rThumb2 rThumb3 rCarpal1 rCarpal2 rCarpal3 rCarpal4
    rIndex1 rIndex2 rIndex3 rMid1 rMid2 rMid3 rRing1 rRing2 rRing3
    rPinky1 rPinky2 rPinky3
    #lFoot lMetatarsals lToe lBigToe lBigToe_2
    #lSmallToe1 lSmallToe1_2 lSmallToe2 lSmallToe2_2 lSmallToe3 lSmallToe3_2
    #lSmallToe4 lSmallToe4_2
    #rFoot rMetatarsals rToe rBigToe rBigToe_2
    #rSmallToe1 rSmallToe1_2 rSmallToe2 rSmallToe2_2 rSmallToe3 rSmallToe3_2
    #rSmallToe4 rSmallToe4_2
)

[ -x "$BIN" ] || cmake --build build -j --target bone_probe
grep -q '^-- BEGIN GENERATED' "$LUA" || { echo "error: $LUA has no -- BEGIN GENERATED marker" >&2; exit 1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"$BIN" "$GLB" --threshold="$THRESHOLD" "${BONES[@]}" > "$tmp/probe.txt"

# table section = everything from 'local kFits' to end of output (drops the header diagnostic)
sed -n '/^local kFits = {/,$p' "$tmp/probe.txt" > "$tmp/tables.txt"

# replace the marker region with the fresh tables
awk -v tf="$tmp/tables.txt" '
    /^-- BEGIN GENERATED/ {
        print
        while ((getline line < tf) > 0) print line
        close(tf)
        skip = 1
        next
    }
    skip && /^-- END GENERATED/ { skip = 0 }
    skip { next }
    { print }
' "$LUA" > "$tmp/lua.new"
mv "$tmp/lua.new" "$LUA"
echo "updated $LUA from $GLB: $(grep -c 'caps = {' "$tmp/tables.txt") bones, $(grep -c 'parent =' "$tmp/tables.txt") joints"
