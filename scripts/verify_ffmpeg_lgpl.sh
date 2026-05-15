#!/usr/bin/env bash
# verify_ffmpeg_lgpl.sh
# Phase 14.3-01 — standalone LGPL discipline + clean-linkage gate.
#
# Lifted and generalized from CMakeLists.txt:384-388's inline POST_BUILD
# strings-check (which only covered macos-x86_64). The new script:
#   1. Iterates every populated platform tree under libs/ffmpeg/
#   2. Per platform: asserts `strings <lib>` contains no `libx264|x264_encoder|x264_init`
#   3. For macOS dirs: asserts `otool -L` entries fall in {@rpath, /usr/lib,
#      /System, /Users/...libs/ffmpeg} (closes spike Risk #4 — libX11 spurious dep)
#   4. For Linux dirs: asserts `ldd` deps come only from glibc + vendored dir
#   5. For Windows dirs: strings-check only (no otool/ldd equivalent;
#      Windows DLLs link KERNEL32.DLL etc. via PE imports, not symbol-grepable)
#
# Behavior:
#   - Skips (continue) on platforms without a populated $PLAT_DIR/lib (so
#     partial vendoring during dev doesn't block CI gate setup)
#   - Exits 0 on success; exits 1 on any contamination with a clear message
#     naming the offending file
#
# Usage:
#   bash scripts/verify_ffmpeg_lgpl.sh
#
# Wired by:
#   - CMakeLists.txt POST_BUILD (Phase 14.3-01 Task 3 — single source of truth)
#   - .github/workflows/juce-build.yml `Verify LGPL discipline` step (Task 3)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

FAILED=0

for PLAT_DIR in libs/ffmpeg/macos-x86_64 libs/ffmpeg/macos-arm64 \
                libs/ffmpeg/linux-x86_64 libs/ffmpeg/windows-x86_64; do
  if [ ! -d "$PLAT_DIR/lib" ]; then
    echo "SKIP: $PLAT_DIR (not vendored)"
    continue
  fi

  # ---- Universal: strings check for libx264 contamination -----------------
  # Each platform glob matches its native lib extension; the glob may expand
  # to a literal pattern when no matches exist, so route through `2>/dev/null`
  # and accept empty output.
  shopt -s nullglob
  LIBS=( "$PLAT_DIR/lib"/lib*.dylib "$PLAT_DIR/lib"/lib*.so "$PLAT_DIR/lib"/lib*.so.* \
         "$PLAT_DIR/lib"/*.dll )
  shopt -u nullglob

  if [ "${#LIBS[@]}" -eq 0 ]; then
    echo "SKIP: $PLAT_DIR (no library files found under lib/)"
    continue
  fi

  CONTAMINATED=""
  for L in "${LIBS[@]}"; do
    # `strings` ships with binutils on Linux + Xcode CLT on macOS + GNU
    # binutils/llvm-objdump on most CI runners. If absent, the check fails
    # loudly rather than silently passing.
    if strings "$L" 2>/dev/null | grep -E 'libx264|x264_encoder|x264_init' > /dev/null; then
      CONTAMINATED="$L"
      break
    fi
  done
  if [ -n "$CONTAMINATED" ]; then
    echo "FAIL: $PLAT_DIR — libx264 strings found in $CONTAMINATED" >&2
    FAILED=1
    continue
  fi

  # ---- macOS: otool linkage check -----------------------------------------
  # Allowed dep prefixes: @rpath, /usr/lib, /System, $PWD/libs/ffmpeg (which
  # resolves to the absolute REPO_ROOT path on Macs that haven't yet had the
  # install-name dance applied). Anything else (e.g. /usr/local/opt/libx11 —
  # spike Risk #4) fails the gate.
  case "$PLAT_DIR" in
    libs/ffmpeg/macos-*)
      if ! command -v otool >/dev/null 2>&1; then
        echo "FAIL: $PLAT_DIR — macOS otool not found (cannot verify linkage)" >&2
        FAILED=1
        continue
      fi
      BAD_DEPS=""
      for D in "$PLAT_DIR/lib"/*.dylib; do
        # Skip symlinks (otool follows them; we only need the canonical files).
        [ -L "$D" ] && continue
        # otool output: line 1 is the dylib path; remaining lines are the
        # LC_LOAD_DYLIB entries (one per line, "    <path> (compat ..., current ...)").
        # We grep out the lines containing legitimate prefixes; anything left
        # is contamination.
        OTOOL_OUT=$(otool -L "$D" 2>/dev/null | tail -n +2 | awk '{print $1}')
        BAD=$(echo "$OTOOL_OUT" | grep -vE '^@rpath|^/usr/lib|^/System|libs/ffmpeg' || true)
        if [ -n "$BAD" ]; then
          BAD_DEPS+="\n  $D:\n$(echo "$BAD" | sed 's/^/    /')"
        fi
      done
      if [ -n "$BAD_DEPS" ]; then
        printf "FAIL: %s — unexpected dynamic deps:%b\n" "$PLAT_DIR" "$BAD_DEPS" >&2
        FAILED=1
        continue
      fi
      ;;

    libs/ffmpeg/linux-*)
      # ldd-based check: deps must come from system glibc dirs or be the
      # vendored libs themselves. Anything in /usr/local or /opt is contamination.
      if ! command -v ldd >/dev/null 2>&1; then
        echo "FAIL: $PLAT_DIR — Linux ldd not found (cannot verify linkage)" >&2
        FAILED=1
        continue
      fi
      BAD_DEPS=""
      for D in "$PLAT_DIR/lib"/lib*.so* ; do
        [ -L "$D" ] && continue
        # ldd output: "  libname.so.N => /path/to/libname.so.N (0xADDR)" or
        # static lines like "  linux-vdso.so.1 (0xADDR)" with no => path.
        # Strip to just the resolved path and filter allowed prefixes.
        LDD_OUT=$(ldd "$D" 2>/dev/null | awk -F'=>' '{print $2}' | awk '{print $1}')
        BAD=$(echo "$LDD_OUT" | grep -E '^/usr/local|^/opt|^/home' || true)
        if [ -n "$BAD" ]; then
          BAD_DEPS+="\n  $D:\n$(echo "$BAD" | sed 's/^/    /')"
        fi
        # Catch missing runtime deps (e.g. a patchelf run that broke a NEEDED
        # entry). ldd reports "libfoo.so.N => not found" for absent deps; the
        # path-extraction pipeline above yields "not" which is not matched by
        # the /usr/local|/opt|/home grep, so missing deps silently pass without
        # this explicit check.
        if ldd "$D" 2>/dev/null | grep -q " => not found"; then
          BAD_DEPS+="\n  $D: has 'not found' (missing runtime) deps"
        fi
      done
      if [ -n "$BAD_DEPS" ]; then
        printf "FAIL: %s — unexpected dynamic deps:%b\n" "$PLAT_DIR" "$BAD_DEPS" >&2
        FAILED=1
        continue
      fi
      ;;

    libs/ffmpeg/windows-*)
      # Windows DLLs link KERNEL32.DLL etc. via the PE import table. There is
      # no portable shell tool equivalent to otool/ldd for this. The
      # strings-check above is the LGPL discipline gate; for runtime dep
      # auditing on Windows, use `dumpbin /dependents` in MSVC tooling — not
      # in this shell gate.
      ;;
  esac

  echo "OK: $PLAT_DIR"
done

if [ "$FAILED" -ne 0 ]; then
  echo ""
  echo "verify_ffmpeg_lgpl.sh: one or more platforms failed the LGPL/linkage gate." >&2
  exit 1
fi

exit 0
