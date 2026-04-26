#!/usr/bin/env bash
#
# JamWide dev build wrapper.
#
# Usage:
#   ./scripts/build.sh                     # build all JUCE targets (Release)
#   ./scripts/build.sh JamWideJuce_VST3    # build only one target
#   (other targets: JamWideJuce_AU, JamWideJuce_CLAP, JamWideJuce_Standalone)
#   ./scripts/build.sh --debug         # build Debug instead of Release
#   ./scripts/build.sh --tests         # build the test binaries (build-test/)
#   ./scripts/build.sh --tsan          # build-tsan/ with ThreadSanitizer (-fsanitize=thread,
#                                      # JAMWIDE_BUILD_TESTS=ON + JAMWIDE_BUILD_JUCE=ON +
#                                      # JAMWIDE_TSAN=ON, codesign skipped). Default target:
#                                      # JamWideJuce_Standalone. Single TSan target covers
#                                      # NJClient core unit tests AND the JUCE callback boundary.
#   ./scripts/build.sh --reconfigure   # nuke + reconfigure (e.g. to switch to Ninja)
#   ./scripts/build.sh --universal     # macOS only: opt back into universal (arm64+x86_64)
#
# macOS local default: x86_64-only (matches Homebrew openssl arch). CI builds
# universal via .github/workflows/juce-build.yml, which doesn't use this script.
# Auto-detects Ninja if installed; falls back to the default generator otherwise.
# Auto-parallelism: uses sysctl on macOS, nproc on Linux.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${REPO_ROOT}"

BUILD_DIR="build-juce"
BUILD_TYPE="Release"
RECONFIGURE=0
TESTS=0
TSAN=0
UNIVERSAL=0
TARGETS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug)       BUILD_TYPE="Debug"; shift ;;
        --release)     BUILD_TYPE="Release"; shift ;;
        --reconfigure) RECONFIGURE=1; shift ;;
        --tests)       TESTS=1; BUILD_DIR="build-test"; shift ;;
        --tsan)        TSAN=1; BUILD_DIR="build-tsan"; BUILD_TYPE="Debug"; shift ;;
        --universal)   UNIVERSAL=1; shift ;;
        --build-dir)   BUILD_DIR="$2"; shift 2 ;;
        -h|--help)     sed -n '2,22p' "$0"; exit 0 ;;
        *)             TARGETS+=("$1"); shift ;;
    esac
done

# Pick generator: Ninja if available, else CMake's default for this platform.
if command -v ninja >/dev/null 2>&1; then
    GENERATOR="Ninja"
else
    GENERATOR=""  # let CMake pick
fi

# Job count: Ninja handles parallelism itself; for Make we pass -j explicitly.
if [[ "$(uname)" == "Darwin" ]]; then
    JOBS="$(sysctl -n hw.ncpu)"
else
    JOBS="$(nproc 2>/dev/null || echo 8)"
fi

if [[ "${RECONFIGURE}" -eq 1 && -d "${BUILD_DIR}" ]]; then
    echo "[build] --reconfigure: removing ${BUILD_DIR}/"
    rm -rf "${BUILD_DIR}"
fi

# Configure if cache is missing.
if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "[build] configuring ${BUILD_DIR} (generator: ${GENERATOR:-default}, build type: ${BUILD_TYPE})"
    CFG_ARGS=(-S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}")
    [[ -n "${GENERATOR}" ]] && CFG_ARGS+=(-G "${GENERATOR}")
    if [[ "${TESTS}" -eq 1 ]]; then
        CFG_ARGS+=(-DJAMWIDE_BUILD_TESTS=ON -DJAMWIDE_BUILD_JUCE=OFF)
    fi
    # 15.1-04: TSan single-target covers NJClient core unit tests AND the JUCE
    # callback boundary. JAMWIDE_HARDENED_RUNTIME forced OFF and codesign
    # gated off in CMakeLists.txt (NOT JAMWIDE_TSAN clause) — TSan injects a
    # runtime not covered by ad-hoc codesigning, see 15.1-RESEARCH macOS caveat #1.
    if [[ "${TSAN}" -eq 1 ]]; then
        CFG_ARGS+=(-DJAMWIDE_BUILD_TESTS=ON -DJAMWIDE_BUILD_JUCE=ON -DJAMWIDE_TSAN=ON)
        CFG_ARGS+=(-DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1 -fno-omit-frame-pointer")
        CFG_ARGS+=(-DCMAKE_C_FLAGS="-fsanitize=thread -g -O1 -fno-omit-frame-pointer")
        CFG_ARGS+=(-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread")
        CFG_ARGS+=(-DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=thread")
        CFG_ARGS+=(-DJAMWIDE_HARDENED_RUNTIME=OFF)
    fi
    # macOS local default: x86_64-only (Homebrew openssl is single-arch).
    # CI runs cmake directly without this script, so universal there is unaffected.
    if [[ "$(uname)" == "Darwin" && "${UNIVERSAL}" -eq 0 ]]; then
        CFG_ARGS+=(-DJAMWIDE_UNIVERSAL=OFF -DCMAKE_OSX_ARCHITECTURES=x86_64)
        echo "[build] macOS local profile: x86_64-only (use --universal to override)"
    fi
    cmake "${CFG_ARGS[@]}"
else
    # Warn if existing cache uses a different generator than what we'd pick now.
    EXISTING="$(grep -E '^CMAKE_GENERATOR:' "${BUILD_DIR}/CMakeCache.txt" | sed 's/.*=//')"
    if [[ -n "${GENERATOR}" && "${EXISTING}" != "${GENERATOR}" ]]; then
        echo "[build] note: ${BUILD_DIR} is configured for '${EXISTING}', but '${GENERATOR}' is available."
        echo "[build] run './scripts/build.sh --reconfigure' to switch (deletes ${BUILD_DIR}/)."
    fi
fi

# 15.1-04: default --tsan target is the standalone (covers JUCE callback boundary).
# Plain `./scripts/build.sh --tsan` should produce a TSan-instrumented standalone.
if [[ "${TSAN}" -eq 1 && ${#TARGETS[@]} -eq 0 ]]; then
    TARGETS=("JamWideJuce_Standalone")
fi

# Build.
BUILD_ARGS=(--build "${BUILD_DIR}" --config "${BUILD_TYPE}")
if [[ ${#TARGETS[@]} -gt 0 ]]; then
    BUILD_ARGS+=(--target "${TARGETS[@]}")
fi
# Pass -j only for non-Ninja generators (Ninja parallelizes by default).
EXISTING_GEN="$(grep -E '^CMAKE_GENERATOR:' "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null | sed 's/.*=//')"
if [[ "${EXISTING_GEN}" != "Ninja" ]]; then
    BUILD_ARGS+=(-j"${JOBS}")
fi

echo "[build] cmake ${BUILD_ARGS[*]}"
exec cmake "${BUILD_ARGS[@]}"
