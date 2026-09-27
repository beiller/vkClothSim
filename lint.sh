#!/usr/bin/env bash
# lint.sh — the code linters for 3dsim.
#   ./lint.sh     check: clang-format (fails on diffs) + clang-tidy over src/ + glslang over shaders/
#   ./lint.sh -f  same, but also rewrites formatting in place
# The vendored libraries (lib/) are never linted. clang-tidy uses the CMake compile
# database (build/compile_commands.json; reconfigured automatically if missing).
set -eu
cd "$(dirname "$0")"

if [ ! -f build/compile_commands.json ]; then
    cmake -S . -B build -Wno-dev
fi

SOURCES=$(find src -name '*.cpp' -o -name '*.hpp')

echo "== clang-format =="
if [ "${1:-}" = "-f" ]; then
    clang-format -i $SOURCES
else
    clang-format --dry-run -Werror $SOURCES
fi

# clang-tidy: only the translation units that are in the compile database AND live under
# src/ — the vendored lib/ is never linted (the DB filter here + the HeaderFilterRegex in
# .clang-tidy keep it out; note the vksim target compiles lib/imgui sources too).
# Dedupe TUs shared by several targets.
DBFILES=$(python3 - <<'EOF'
import json
seen = set()
for e in json.load(open("build/compile_commands.json")):
    f = e["file"]
    if "/3dsim/src/" in f and f not in seen:
        seen.add(f)
        print(f)
EOF
)

echo "== clang-tidy ($(echo $DBFILES | wc -w) TUs) =="
# shellcheck disable=SC2086
clang-tidy -p build --header-filter='3dsim/src/' $DBFILES

# glslangValidator validates each shader against the Vulkan target (the shaders use
# Vulkan builtins like gl_VertexIndex / push_constant, which the default OpenGL target
# rejects). Stage is inferred from the file extension (.vert/.frag/.comp).
SHADERS=$(find shaders -name '*.vert' -o -name '*.frag' -o -name '*.comp')

echo "== glsl ($(echo $SHADERS | wc -w) shaders) =="
# Validate each shader on its own: passing several files makes glslang link them into one
# pipeline, which fails once a stage has more than one entry point (e.g. two .vert files).
for s in $SHADERS; do
    glslangValidator --target-env vulkan1.1 "$s"
done

echo "== ok =="
