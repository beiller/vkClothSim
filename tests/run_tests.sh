#!/bin/sh
# 3dsim test suite: C++ unit binaries + headless scene tests (tests/scenes/*.json).
# Scene verdict contract: the run must exit 0, print "TTEST pass=N fail=0" (N >= 1),
# and contain no FAIL / script-error lines.
cd "$(dirname "$0")/.." || exit 1

fail=0

if ! cmake --build build -j8 > /tmp/vksim_build.log 2>&1; then
    echo "FAIL build (see /tmp/vksim_build.log)"
    exit 1
fi

for bin in dualquat_test view_proj_test collision_shape_solver_test; do
    if "./build/$bin" > "/tmp/${bin}.log" 2>&1; then
        echo "PASS $bin"
    else
        echo "FAIL $bin (see /tmp/${bin}.log)"
        fail=1
    fi
done

# scene|frames (headless: one physics tick per frame)
for spec in \
    "find_probe|5" \
    "skin_probe|5" \
    "shape_probe|5" \
    "lua_skin|5" \
    "dynquat_check|45" \
    "force_check|245" \
    "weld_check|65" \
    "sixdof|155" \
    "tcap_check|300" \
    "rope_check|130" \
    "gltf_table_check|5" \
    "decomp_check|600" \
    "sword_diag|5"
do
    name=${spec%%|*}
    frames=${spec##*|}
    log="/tmp/vksim_${name}.log"
    ./build/vksim --headless --scenes-dir tests/scenes --scene "$name" --frames "$frames" > "$log" 2>&1
    rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "FAIL ${name} (exit $rc; see $log)"
        fail=1
        continue
    fi
    if grep -q "^TTEST pass=[1-9][0-9]* fail=0$" "$log" && ! grep -q "^FAIL \|^3dsim: script " "$log"; then
        echo "PASS ${name}"
    else
        echo "FAIL ${name} (verdict missing or failed; see $log)"
        fail=1
    fi
done

# graphics-required scene (windowed; needs a display)
if [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
    echo "SKIP tex_check (no display)"
else
    log="/tmp/vksim_tex_check.log"
    ./build/vksim --scenes-dir tests/scenes --scene tex_check --frames 12 > "$log" 2>&1
    rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "FAIL tex_check (exit $rc; see $log)"
        fail=1
    elif grep -q "^TTEST pass=[1-9][0-9]* fail=0$" "$log" && ! grep -q "^FAIL \|^3dsim: script " "$log"; then
        echo "PASS tex_check"
    else
        echo "FAIL tex_check (verdict missing or failed; see $log)"
        fail=1
    fi
fi

if [ "$fail" -eq 0 ]; then
    echo "ALL TESTS PASS"
else
    echo "TESTS FAILED"
fi
exit "$fail"
