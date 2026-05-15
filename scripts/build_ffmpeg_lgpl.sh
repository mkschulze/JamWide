#!/usr/bin/env bash
# build_ffmpeg_lgpl.sh
# Phase 14.3-01 — cross-platform LGPL ffmpeg + Cisco openh264 vendoring.
#
# Lineage: started life in quick task 260515-0pc as a single-platform
# macOS-x86_64 spike. Phase 14.3-01 extended it to four legs:
#   (Darwin, x86_64) → libs/ffmpeg/macos-x86_64/
#   (Darwin, arm64)  → libs/ffmpeg/macos-arm64/      (Cisco openh264 from source)
#   (Linux,  x86_64) → libs/ffmpeg/linux-x86_64/
#   (MINGW,  x86_64) → libs/ffmpeg/windows-x86_64/
#
# DISTRIBUTION STRATEGY: PATH A (build ffmpeg from source).
# Reason: brew's prebuilt ffmpeg is GPL-tainted (--enable-gpl --enable-libx264
# present in `ffmpeg -version`). JamWide is non-GPL; bundling brew's ffmpeg
# dylibs would force a license change. This script builds a minimal LGPL-only
# ffmpeg from source and bundles the Cisco prebuilt openh264 binary (Cisco's
# binary inherits Cisco's MPEG-LA royalty payment; see openh264 leg notes
# below for the arm64-mac exception).
#
# CISCO openh264 PREBUILT NOTE: Cisco's last release with prebuilt mac dylibs
# is v2.1.1 (osx32, osx64). v2.2.0+ ships source-only.
#   - x86_64 macOS/Linux/Windows → use Cisco's v2.1.1 prebuilt (inherits Cisco
#     MPEG-LA royalty payment).
#   - arm64 macOS → no Cisco prebuilt at any version. Build openh264 v2.1.1
#     from source via Cisco's official Makefile. This deviates from Cisco's
#     binary-distribution royalty model — JamWide may need to register with
#     MPEG-LA AVC patent pool for the arm64-mac slice if shipping
#     commercially at scale (per Phase 14.3 SPEC §"Locked Decisions" #5).
#     Manageable: <100k licenses/year is below MPEG-LA cap threshold.
#
# OUTPUT (per platform tag, $PREFIX=libs/ffmpeg/${PLATFORM_TAG}):
#   $PREFIX/lib/lib{avcodec,avformat,swscale,avutil,openh264}.{dylib|so.*|dll}
#   $PREFIX/include/{libavcodec,libavformat,libswscale,libavutil,wels}/
#   libs/ffmpeg/configure-flags.txt   (literal ./configure invocation used)
#   libs/ffmpeg/LICENSE.LGPL.txt      (verbatim from ffmpeg's COPYING.LGPLv2.1)
#   libs/ffmpeg/LICENSE.openh264.txt  (verbatim from openh264's LICENSE)
#
# Roughly 10–25 minutes wall-clock on a recent 8-core machine with --enable-asm.
#
# VERIFY: scripts/verify_ffmpeg_lgpl.sh runs the LGPL discipline gate against
# every populated platform tree (no libx264 strings; clean otool/ldd linkage).

set -euo pipefail

# ---- Configuration --------------------------------------------------------
FFMPEG_VERSION="${FFMPEG_VERSION:-7.1.2}"
OPENH264_VERSION="${OPENH264_VERSION:-2.1.1}"   # last Cisco-prebuilt mac release

# Detect (OS, ARCH) and map to PLATFORM_TAG / CISCO leg / library extension.
OS="$(uname -s)"
ARCH="$(uname -m)"

# PLATFORM_TAG    — subdir under libs/ffmpeg/
# CISCO_PREBUILT  — Cisco openh264 v2.1.1 prebuilt tag (or "source-build")
# LIB_EXT         — primary shared-library extension for this platform
# OPENH264_SO_NAME — runtime-loaded openh264 filename (consumers find it via @rpath/$PATH)
case "$OS|$ARCH" in
  "Darwin|x86_64")
    PLATFORM_TAG="macos-x86_64"
    CISCO_PREBUILT="osx64"
    LIB_EXT="dylib"
    OPENH264_SO_NAME="libopenh264.6.dylib"
    ;;
  "Darwin|arm64")
    PLATFORM_TAG="macos-arm64"
    CISCO_PREBUILT="source-build"   # Cisco does not ship arm64 mac prebuilts
    LIB_EXT="dylib"
    OPENH264_SO_NAME="libopenh264.6.dylib"
    ;;
  "Linux|x86_64")
    PLATFORM_TAG="linux-x86_64"
    CISCO_PREBUILT="linux64"
    LIB_EXT="so"
    OPENH264_SO_NAME="libopenh264.so.6"
    ;;
  MINGW64_NT-*"|x86_64"|MSYS_NT-*"|x86_64"|CYGWIN_NT-*"|x86_64")
    PLATFORM_TAG="windows-x86_64"
    CISCO_PREBUILT="win64"
    LIB_EXT="dll"
    OPENH264_SO_NAME="openh264-6.dll"  # Cisco's win64 prebuilt filename
    ;;
  *)
    echo "ERROR: unsupported (OS, ARCH) tuple: $OS|$ARCH" >&2
    echo "       Supported: (Darwin, x86_64|arm64), (Linux, x86_64), (MINGW64_NT-*, x86_64)" >&2
    exit 1
    ;;
esac

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${REPO_ROOT}/libs/ffmpeg/${PLATFORM_TAG}"
WORK="${REPO_ROOT}/.bg-shell/ffmpeg-build-${PLATFORM_TAG}"

case "$OS" in
  Darwin) NCPU="$(sysctl -n hw.ncpu)" ;;
  Linux)  NCPU="$(nproc)" ;;
  *)      NCPU=4 ;;
esac

ASM_FLAG="--enable-asm"
if ! command -v nasm >/dev/null 2>&1 && ! command -v yasm >/dev/null 2>&1; then
  echo "WARNING: nasm/yasm not found — falling back to --disable-asm" >&2
  ASM_FLAG="--disable-asm"
fi

mkdir -p "$WORK" "$PREFIX/lib" "$PREFIX/include"
cd "$WORK"

# ---- Step 1: Cisco openh264 (prebuilt or source) --------------------------
echo "==> [1/5] Provisioning Cisco openh264 v${OPENH264_VERSION} for ${PLATFORM_TAG}"

if [ "$CISCO_PREBUILT" = "source-build" ]; then
  # macOS-arm64 path: build openh264 v2.1.1 from source via Cisco's Makefile.
  # MPEG-LA royalty payment NOT inherited (Cisco only pays for their distributed
  # binaries) — see comment block at top of this file.
  echo "    (no Cisco prebuilt for $PLATFORM_TAG — building openh264 from source)"
  if [ ! -d "openh264-src" ]; then
    git clone --depth=1 --branch "v${OPENH264_VERSION}" \
      https://github.com/cisco/openh264.git openh264-src
  fi
  (
    cd openh264-src
    case "$OS|$ARCH" in
      "Darwin|arm64")
        make OS=darwin64 ARCH=arm64 "-j${NCPU}"
        # Build outputs ./libopenh264.6.dylib in the source tree
        cp -f libopenh264.6.dylib "$WORK/$OPENH264_SO_NAME"
        ;;
      *)
        echo "ERROR: source-build path not implemented for $OS|$ARCH" >&2
        exit 1
        ;;
    esac
  )
  ls -lh "$WORK/$OPENH264_SO_NAME"
else
  # x86_64 paths use Cisco's GitHub-release prebuilts.
  case "$LIB_EXT" in
    dylib)
      DYLIB_BZ2="libopenh264-${OPENH264_VERSION}-${CISCO_PREBUILT}.6.dylib.bz2"
      DYLIB_RAW="libopenh264-${OPENH264_VERSION}-${CISCO_PREBUILT}.6.dylib"
      if [ ! -f "$DYLIB_BZ2" ]; then
        curl -fsSL -o "$DYLIB_BZ2" \
          "https://github.com/cisco/openh264/releases/download/v${OPENH264_VERSION}/${DYLIB_BZ2}"
      fi
      if [ ! -f "$DYLIB_RAW" ]; then
        bunzip2 -k "$DYLIB_BZ2"
      fi
      cp -f "$DYLIB_RAW" "$OPENH264_SO_NAME"
      ;;
    so)
      SO_BZ2="libopenh264-${OPENH264_VERSION}-${CISCO_PREBUILT}.6.so.bz2"
      SO_RAW="libopenh264-${OPENH264_VERSION}-${CISCO_PREBUILT}.6.so"
      if [ ! -f "$SO_BZ2" ]; then
        curl -fsSL -o "$SO_BZ2" \
          "https://github.com/cisco/openh264/releases/download/v${OPENH264_VERSION}/${SO_BZ2}"
      fi
      if [ ! -f "$SO_RAW" ]; then
        bunzip2 -k "$SO_BZ2"
      fi
      cp -f "$SO_RAW" "$OPENH264_SO_NAME"
      ;;
    dll)
      DLL_BZ2="openh264-${OPENH264_VERSION}-${CISCO_PREBUILT}.dll.bz2"
      DLL_RAW="openh264-${OPENH264_VERSION}-${CISCO_PREBUILT}.dll"
      if [ ! -f "$DLL_BZ2" ]; then
        curl -fsSL -o "$DLL_BZ2" \
          "https://github.com/cisco/openh264/releases/download/v${OPENH264_VERSION}/${DLL_BZ2}"
      fi
      if [ ! -f "$DLL_RAW" ]; then
        bunzip2 -k "$DLL_BZ2"
      fi
      cp -f "$DLL_RAW" "$OPENH264_SO_NAME"
      ;;
  esac
  ls -lh "$WORK/$OPENH264_SO_NAME"
fi

# Clone openh264 source for headers (every leg needs codec_api.h etc.).
if [ ! -d "openh264-src" ]; then
  echo "    cloning openh264 source for headers (v${OPENH264_VERSION} tag)..."
  git clone --depth=1 --branch "v${OPENH264_VERSION}" \
    https://github.com/cisco/openh264.git openh264-src
fi

# Synthetic prefix so ffmpeg's configure picks up openh264 via pkg-config.
mkdir -p "$WORK/openh264-prefix/include/wels" "$WORK/openh264-prefix/lib/pkgconfig"
# v2.1.1 headers live at codec/api/svc/ (newer versions moved them to wels/).
# Copy them under wels/ which is where ffmpeg's `#include <wels/codec_*.h>` expects them.
if [ -d openh264-src/codec/api/wels ]; then
  cp openh264-src/codec/api/wels/*.h "$WORK/openh264-prefix/include/wels/"
elif [ -d openh264-src/codec/api/svc ]; then
  cp openh264-src/codec/api/svc/*.h "$WORK/openh264-prefix/include/wels/"
else
  echo "FATAL: cannot locate openh264 codec_api.h" >&2; exit 1
fi
# Stage the openh264 shared library + a generic-name symlink the linker uses.
cp "$OPENH264_SO_NAME" "$WORK/openh264-prefix/lib/$OPENH264_SO_NAME"
case "$LIB_EXT" in
  dylib) ln -sf "$OPENH264_SO_NAME" "$WORK/openh264-prefix/lib/libopenh264.dylib" ;;
  so)    ln -sf "$OPENH264_SO_NAME" "$WORK/openh264-prefix/lib/libopenh264.so" ;;
  dll)   : ;;  # Windows: no symlinks; consumer references the canonical name
esac

cat > "$WORK/openh264-prefix/lib/pkgconfig/openh264.pc" <<PKG
prefix=${WORK}/openh264-prefix
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: OpenH264
Description: OpenH264 is a codec library which supports H.264 encoding and decoding.
Version: ${OPENH264_VERSION}
Libs: -L\${libdir} -lopenh264
Cflags: -I\${includedir}
PKG

# ---- Step 2: ffmpeg source ------------------------------------------------
# T-14.3-01 supply-chain mitigation: pin the SHA-256 of the canonical
# ffmpeg.org-served ffmpeg-${FFMPEG_VERSION}.tar.xz. Verified locally on
# 2026-05-15 against https://ffmpeg.org/releases/ffmpeg-7.1.2.tar.xz
# (10,985 KB / 11,030,368 bytes). When bumping FFMPEG_VERSION, recompute via:
#   curl -fsSL "https://ffmpeg.org/releases/ffmpeg-${NEW}.tar.xz" | shasum -a 256
# and update EXPECTED_FFMPEG_SHA256 in the same commit.
# Override at run time: FFMPEG_TARBALL_SHA256=<hash> bash scripts/build_ffmpeg_lgpl.sh
EXPECTED_FFMPEG_SHA256="${FFMPEG_TARBALL_SHA256:-089bc60fb59d6aecc5d994ff530fd0dcb3ee39aa55867849a2bbc4e555f9c304}"

echo "==> [2/5] Fetching ffmpeg source v${FFMPEG_VERSION}"
if [ ! -d "ffmpeg-${FFMPEG_VERSION}" ]; then
  if [ ! -f "ffmpeg-${FFMPEG_VERSION}.tar.xz" ]; then
    curl -fsSL -o "ffmpeg-${FFMPEG_VERSION}.tar.xz" \
      "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz"
  fi
  # Supply-chain integrity gate. Hash mismatch => abort BEFORE extracting,
  # so a tampered tarball never reaches the configure/build step.
  ACTUAL_SHA256="$(shasum -a 256 "ffmpeg-${FFMPEG_VERSION}.tar.xz" | awk '{print $1}')"
  if [ "$ACTUAL_SHA256" != "$EXPECTED_FFMPEG_SHA256" ]; then
    echo "FATAL: ffmpeg tarball SHA-256 mismatch (T-14.3-01 supply-chain gate)" >&2
    echo "  expected: $EXPECTED_FFMPEG_SHA256" >&2
    echo "  actual:   $ACTUAL_SHA256" >&2
    echo "  file:     $(pwd)/ffmpeg-${FFMPEG_VERSION}.tar.xz" >&2
    echo "Refusing to extract. If the version was intentionally bumped, update" >&2
    echo "EXPECTED_FFMPEG_SHA256 in scripts/build_ffmpeg_lgpl.sh in the same commit." >&2
    exit 1
  fi
  echo "    sha256: $ACTUAL_SHA256 (verified)"
  tar -xJf "ffmpeg-${FFMPEG_VERSION}.tar.xz"
fi
cd "ffmpeg-${FFMPEG_VERSION}"

# ---- Step 3: Configure ----------------------------------------------------
echo "==> [3/5] Configuring ffmpeg (LGPL, openh264, no x264, no GPL components)"
# IMPORTANT: ffmpeg processes configure flags left-to-right; --disable-everything
# MUST come BEFORE the per-component --enable-* flags so the enables take effect.
# Putting it last would silently re-disable everything we just enabled.
# (Locked LGPL flag set per Phase 14.3 SPEC §"Locked Decisions" #4 — order preserved.)
CONFIG_CMD="./configure \
  --prefix=${PREFIX} \
  --extra-cflags=-I${WORK}/openh264-prefix/include \
  --extra-ldflags=-L${WORK}/openh264-prefix/lib \
  --enable-shared --disable-static --disable-gpl --disable-nonfree \
  --disable-programs --disable-doc --disable-avdevice --disable-swresample \
  --disable-avfilter --disable-postproc --disable-network \
  --disable-xlib --disable-libxcb --disable-sdl2 \
  --disable-everything \
  --enable-libopenh264 --enable-encoder=libopenh264 --enable-decoder=h264 \
  --enable-protocol=file --enable-demuxer=h264 --enable-muxer=h264 \
  --enable-parser=h264 --enable-swscale \
  ${ASM_FLAG}"

# Save the literal configure invocation for compliance + reproducibility.
{
  echo "# ffmpeg LGPL configure invocation — used to vendor libs/ffmpeg/${PLATFORM_TAG}/"
  echo "# Generated by scripts/build_ffmpeg_lgpl.sh on $(date -u +%FT%TZ)"
  echo "# ffmpeg version: ${FFMPEG_VERSION}"
  echo "# openh264 version: ${OPENH264_VERSION} (Cisco ${CISCO_PREBUILT} — see script header for royalty notes)"
  echo "# build host: $(uname -srm)"
  echo "# nasm/yasm asm flag: ${ASM_FLAG}"
  echo ""
  echo "$CONFIG_CMD"
} > "${REPO_ROOT}/libs/ffmpeg/configure-flags.txt"

env "PKG_CONFIG_PATH=${WORK}/openh264-prefix/lib/pkgconfig:${PKG_CONFIG_PATH:-}" \
  bash -c "$CONFIG_CMD" 2>&1 | tail -40

# ---- Step 4: Build --------------------------------------------------------
echo "==> [4/5] Building ffmpeg (-j${NCPU}). Takes ~10-25 min."
make "-j${NCPU}" 2>&1 | tail -10
make install 2>&1 | tail -5

# ---- Step 5: Stage outputs + per-platform install-name discipline --------
echo "==> [5/5] Staging Cisco openh264 + license texts + install-name rewrites"
cp "$WORK/$OPENH264_SO_NAME" "$PREFIX/lib/$OPENH264_SO_NAME"
chmod +w "$PREFIX/lib/$OPENH264_SO_NAME"

# Per-platform install-name discipline (Landmine L5 — openh264 v2.1.1's
# install_name is hardcoded to /usr/local/lib/libopenh264.6.dylib; without
# a rewrite the consumer dlopens the system path at runtime).
case "$OS" in
  Darwin)
    install_name_tool -id "@rpath/$OPENH264_SO_NAME" "$PREFIX/lib/$OPENH264_SO_NAME"
    ln -sf "$OPENH264_SO_NAME" "$PREFIX/lib/libopenh264.dylib"

    # Strip + set @rpath install names on ffmpeg dylibs. Also rewrite ALL
    # inter-dylib references to @rpath form (ffmpeg's --prefix bakes absolute
    # paths into the LC_LOAD_DYLIB commands; for vendored distribution we
    # want the consumer to find them via -rpath).
    for d in "$PREFIX/lib"/*.dylib; do
      if [ ! -L "$d" ]; then
        chmod +w "$d"
        strip -x "$d" 2>/dev/null || true
        install_name_tool -id "@rpath/$(basename "$d")" "$d" 2>/dev/null || true
        for ref in $(otool -L "$d" 2>/dev/null | tail -n +2 | awk '{print $1}'); do
          case "$ref" in
            ${PREFIX}/lib/*|${REPO_ROOT}/libs/ffmpeg/*)
              install_name_tool -change "$ref" "@rpath/$(basename "$ref")" "$d" 2>/dev/null || true ;;
            /usr/local/lib/libopenh264.6.dylib)
              install_name_tool -change "$ref" "@rpath/libopenh264.6.dylib" "$d" 2>/dev/null || true ;;
          esac
        done
      fi
    done
    ;;

  Linux)
    # patchelf-based soname + needed-dep rewrites. Same shape as macOS
    # install_name_tool, but Linux idiom (Landmine L5).
    if ! command -v patchelf >/dev/null 2>&1; then
      echo "ERROR: patchelf required for Linux install-name discipline" >&2
      exit 1
    fi
    # openh264 stays at its native soname; only fix inter-dep references on
    # the ffmpeg .so files below.
    ln -sf "$OPENH264_SO_NAME" "$PREFIX/lib/libopenh264.so"
    for d in "$PREFIX/lib"/lib*.so*; do
      if [ ! -L "$d" ]; then
        for ref in $(patchelf --print-needed "$d" 2>/dev/null); do
          case "$ref" in
            /usr/local/lib/libopenh264.so.6|libopenh264.so.6)
              patchelf --replace-needed "$ref" "$OPENH264_SO_NAME" "$d" 2>/dev/null || true ;;
          esac
        done
      fi
    done
    ;;

  MINGW64_NT-*|MSYS_NT-*|CYGWIN_NT-*)
    # Windows: .dll discovery is via consumer-adjacent path / %PATH%.
    # No install-name rewriting needed. Just verify the .dll files are present.
    echo "    (Windows: .dll files in $PREFIX/lib — consumers discover via PATH)"
    ;;
esac

# (No compat-symlink shimming here — spike-only artifact deleted in Phase 14.3-01.
# cmake/ffmpeg.cmake's file(GLOB ...) + symlink-filter loop resolves real version
# suffixes directly.)

# `make install` deposits ffmpeg's example C sources under share/ffmpeg/examples/
# — useful documentation but not part of our vendored binary distribution.
# Remove to keep the libs/ffmpeg/<platform>/ tree limited to the documented
# OUTPUT (lib/ + include/) above.
rm -rf "$PREFIX/share"

# openh264 headers under canonical wels/ path.
mkdir -p "$PREFIX/include/wels"
if [ -d "$WORK/openh264-src/codec/api/wels" ]; then
  cp "$WORK"/openh264-src/codec/api/wels/*.h "$PREFIX/include/wels/"
elif [ -d "$WORK/openh264-src/codec/api/svc" ]; then
  cp "$WORK"/openh264-src/codec/api/svc/*.h "$PREFIX/include/wels/"
fi

# License files (LGPL 2.1 from ffmpeg, BSD-2 from openh264).
cp "${WORK}/ffmpeg-${FFMPEG_VERSION}/COPYING.LGPLv2.1" "${REPO_ROOT}/libs/ffmpeg/LICENSE.LGPL.txt"
if [ -f "${WORK}/openh264-src/LICENSE" ]; then
  cp "${WORK}/openh264-src/LICENSE" "${REPO_ROOT}/libs/ffmpeg/LICENSE.openh264.txt"
elif [ -f "${WORK}/openh264-src/BINARY_LICENSE.txt" ]; then
  cp "${WORK}/openh264-src/BINARY_LICENSE.txt" "${REPO_ROOT}/libs/ffmpeg/LICENSE.openh264.txt"
else
  curl -fsSL -o "${REPO_ROOT}/libs/ffmpeg/LICENSE.openh264.txt" \
    "https://raw.githubusercontent.com/cisco/openh264/v${OPENH264_VERSION}/LICENSE"
fi

echo ""
echo "==> Build complete. Vendored under: ${PREFIX}"
ls -lh "${PREFIX}/lib/" 2>/dev/null
du -sh "${PREFIX}/lib" "${PREFIX}/include" 2>/dev/null
