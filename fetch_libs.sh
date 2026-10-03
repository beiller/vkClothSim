#!/usr/bin/env bash
# Fetch third-party libs into lib/ (gitignored, not committed).
# Re-runnable: skips a lib whose lib/<dir> already exists. --force to re-fetch.
#
# Pinned to the release versions this project builds against:
#
#   lib      repo                                  ref             version
#   ------   ------------------------------------  --------------  -------------
#   entt     github.com/skypj/entt                 v4.0.0          4.0.0
#   glfw     github.com/glfw/glfw                  3.4             3.4.0
#   imgui    github.com/ocornut/imgui              v1.92.9         1.92.9
#   jolt     github.com/jrouwe/JoltPhysics         v5.6.0          5.6.0
#   openxr   github.com/KhronosGroup/OpenXR-SDK    openxr-1.1.63   XR 1.1.63
#   stb      github.com/nothings/stb               v2.30           stb_image 2.30
#   tinyexr  github.com/syoyo/tinyexr              v1.0.3          1.0.x
#
# A bad ref fails the clone loudly (see the tag list it prints). If a tag has
# moved, bump the REF in the calls at the bottom.
#
# NOTE: the OpenXR *runtime* loader (lib/openxr/lib/libopenxr.so.1) is a
# platform binary and is NOT fetched. It's only needed to RUN VR, not to build.
# Install it from your OpenXR runtime; without it the app falls back to window
# mode (or use --no-vr).

set -euo pipefail
cd "$(dirname "$0")"
command -v git >/dev/null 2>&1 || { echo "error: git is required" >&2; exit 1; }

FORCE=0
[ "${1:-}" = "--force" ] && FORCE=1

clone_at() { # repo ref dest
    local repo=$1 ref=$2 dest=$3
    echo "  [clone] $repo @ $ref"
    rm -rf "$dest"
    if ! git clone --quiet --depth 1 --branch "$ref" "$repo" "$dest"; then
        echo "error: could not clone $repo @ '$ref' (bad ref?)" >&2
        echo "  list tags: git ls-remote --tags $repo" >&2
        exit 1
    fi
}

# full repo clone -> lib/<dir>
fetch_full() { # dir repo ref
    local dir=$1 repo=$2 ref=$3
    local target="lib/$dir"
    if [ -e "$target" ] && [ "$FORCE" -ne 1 ]; then
        echo "  [skip] $target exists (use --force to re-fetch)"
        return
    fi
    clone_at "$repo" "$ref" "$target"
    rm -rf "$target/.git"
}

# clone, then copy specific files (flattened) into lib/<dir>
fetch_files() { # dir repo ref src...
    local dir=$1 repo=$2 ref=$3
    shift 3
    local target="lib/$dir" tmp f
    if [ -e "$target" ] && [ "$FORCE" -ne 1 ]; then
        echo "  [skip] $target exists (use --force to re-fetch)"
        return
    fi
    tmp=$(mktemp -d)
    clone_at "$repo" "$ref" "$tmp"
    rm -rf "$target"
    mkdir -p "$target"
    for f in "$@"; do
        cp "$tmp/$f" "$target/$(basename "$f")"
    done
    rm -rf "$tmp"
}

echo "fetching third-party libs (force=$FORCE)"
fetch_full entt https://github.com/skypj/entt v4.0.0
fetch_full glfw https://github.com/glfw/glfw 3.4
fetch_full imgui https://github.com/ocornut/imgui v1.92.9
fetch_full jolt https://github.com/jrouwe/JoltPhysics v5.6.0
fetch_files openxr https://github.com/KhronosGroup/OpenXR-SDK openxr-1.1.63 \
    include/openxr/openxr.h include/openxr/openxr_platform_defines.h
fetch_files stb https://github.com/nothings/stb v2.30 stb_image.h
fetch_files tinyexr https://github.com/syoyo/tinyexr v1.0.3 tinyexr.h tinyexr.cc

# sanity: each lib's key file is present and non-empty
for f in \
    lib/entt/single_include/entt/entt.hpp \
    lib/glfw/CMakeLists.txt \
    lib/imgui/imgui.h \
    lib/jolt/Jolt/Jolt.cmake \
    lib/openxr/openxr.h \
    lib/openxr/openxr_platform_defines.h \
    lib/stb/stb_image.h \
    lib/tinyexr/tinyexr.h \
    lib/tinyexr/tinyexr.cc
do
    [ -s "$f" ] || { echo "error: $f missing/empty" >&2; exit 1; }
done
echo "done. lib/ ready:"
ls -1 lib/
