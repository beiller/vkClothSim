#!/usr/bin/env bash
# Regenerates the kFits table in assets/scenes/caps_fit.lua from basegen8.glb
# (weight > 0.3, 80-bone set: legs/arms + all finger + all foot bones). No args.
set -euo pipefail
cd "$(dirname "$0")"

BONES="lShin rShin lThighBend rThighBend lThighTwist rThighTwist lShldrBend lShldrTwist lForearmBend lForearmTwist rShldrBend rShldrTwist rForearmBend rForearmTwist"
# finger bones (both hands): palm, thumb, carpal (knuckle bases), index/mid/ring/pinky
BONES="$BONES lHand lThumb1 lThumb2 lThumb3 lCarpal1 lCarpal2 lCarpal3 lCarpal4 lIndex1 lIndex2 lIndex3 lMid1 lMid2 lMid3 lRing1 lRing2 lRing3 lPinky1 lPinky2 lPinky3"
BONES="$BONES rHand rThumb1 rThumb2 rThumb3 rCarpal1 rCarpal2 rCarpal3 rCarpal4 rIndex1 rIndex2 rIndex3 rMid1 rMid2 rMid3 rRing1 rRing2 rRing3 rPinky1 rPinky2 rPinky3"
# foot bones (both feet): foot, metatarsals, toe base, big toe, small toes 1-4 (+_2 phalanx)
BONES="$BONES lFoot lMetatarsals lToe lBigToe lBigToe_2 lSmallToe1 lSmallToe1_2 lSmallToe2 lSmallToe2_2 lSmallToe3 lSmallToe3_2 lSmallToe4 lSmallToe4_2"
BONES="$BONES rFoot rMetatarsals rToe rBigToe rBigToe_2 rSmallToe1 rSmallToe1_2 rSmallToe2 rSmallToe2_2 rSmallToe3 rSmallToe3_2 rSmallToe4 rSmallToe4_2"

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

./build/bone_probe assets/local/basegen8.glb --threshold=0.3 $BONES 2>/dev/null | sed -n '/^local kFits/,/^}/p' > "$tmp"

python3 - "$tmp" <<'EOF'
import sys
p = 'assets/scenes/caps_fit.lua'
s = open(p).read()
g = open(sys.argv[1]).read().rstrip('\n')
i = s.index('local kFits = {')
j = s.index('\n}', i) + 2
open(p, 'w').write(s[:i] + g + s[j:])
print('table updated')
EOF

lua5.4 -e "assert(loadfile('assets/scenes/caps_fit.lua')); print('syntax ok')"
