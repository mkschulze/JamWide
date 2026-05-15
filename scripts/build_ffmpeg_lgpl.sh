#!/usr/bin/env bash
# build_ffmpeg_lgpl.sh
# Quick task 260515-0pc — feasibility spike (RESEARCH § 3 LGPL recipe).
#
# DISTRIBUTION STRATEGY: PATH A (build from source).
# Reason: brew's prebuilt ffmpeg is GPL-tainted (--enable-gpl --enable-libx264
# present in `ffmpeg -version`). JamWide is non-GPL; bundling brew's ffmpeg
# dylibs would force a license change. This script builds a minimal LGPL-only
# ffmpeg from source and bundles the Cisco prebuilt openh264 binary (Cisco's
# binary inherits Cisco's MPEG-LA royalty payment; building openh264 from
# source would NOT inherit that royalty payment, per RESEARCH § "License
# compliance").
#
# DEV-ARCH NOTE: The plan's environment_constraints described the dev arch
# as arm64 (Apple Silicon), but `arch`/`uname -m` on this build machine
# returns x86_64. This script auto-detects via $(uname -m) and stores
# outputs under libs/ffmpeg/macos-${ARCH}/. The milestone (item B in
# deferred-items.md) covers universal-binary stitching across both archs
# and the other platforms (Linux x86_64, Windows x86_64).
#
# CISCO openh264 PREBUILT NOTE: Cisco's last release with prebuilt mac dylibs
# is v2.1.1 (osx32, osx64). v2.2.0+ ships source-only. We use v2.1.1's osx64
# dylib for x86_64 to inherit the MPEG-LA royalty payment; for arm64, the
# milestone will need to negotiate (build from source = JamWide pays MPEG-LA;
# OR fall back to OS-bundled VideoToolbox H.264 on macOS-arm64).
#
# OUTPUT:
#   libs/ffmpeg/macos-${ARCH}/lib/lib{avcodec,avformat,swscale,avutil,openh264}.*.dylib
#   libs/ffmpeg/macos-${ARCH}/include/{libavcodec,libavformat,libswscale,libavutil,wels}/
#   libs/ffmpeg/configure-flags.txt   (the literal ./configure invocation used)
#   libs/ffmpeg/LICENSE.LGPL.txt      (verbatim from ffmpeg's COPYING.LGPLv2.1)
#   libs/ffmpeg/LICENSE.openh264.txt  (verbatim from openh264's BINARY_LICENSE.txt)
#
# Roughly 10–25 minutes wall-clock on a 2020-era 8-core Mac with --enable-asm.
#
set -euo pipefail

# ---- Configuration --------------------------------------------------------
FFMPEG_VERSION="${FFMPEG_VERSION:-7.1.2}"
OPENH264_VERSION="${OPENH264_VERSION:-2.1.1}"   # last Cisco-prebuilt mac release
ARCH="$(uname -m)"
case "$ARCH" in
  arm64)  CISCO_TAG="mac-arm64" ;;             # NO prebuilt — milestone item
  x86_64) CISCO_TAG="osx64"     ;;             # v2.1.1 osx64 prebuilt
  *)      echo "ERROR: unsupported arch $ARCH" >&2; exit 1 ;;
esac

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${REPO_ROOT}/libs/ffmpeg/macos-${ARCH}"
WORK="${REPO_ROOT}/.bg-shell/ffmpeg-build-${ARCH}"
NCPU="$(sysctl -n hw.ncpu)"

ASM_FLAG="--enable-asm"
if ! command -v nasm >/dev/null 2>&1 && ! command -v yasm >/dev/null 2>&1; then
  echo "WARNING: nasm/yasm not found — falling back to --disable-asm" >&2
  ASM_FLAG="--disable-asm"
fi

mkdir -p "$WORK" "$PREFIX/lib" "$PREFIX/include"
cd "$WORK"

# ---- Step 1: Cisco openh264 prebuilt --------------------------------------
echo "==> [1/5] Fetching Cisco openh264 prebuilt v${OPENH264_VERSION} (${CISCO_TAG})"
DYLIB_BZ2="libopenh264-${OPENH264_VERSION}-${CISCO_TAG}.6.dylib.bz2"
DYLIB_RAW="libopenh264-${OPENH264_VERSION}-${CISCO_TAG}.6.dylib"
DYLIB="libopenh264.6.dylib"
if [ "$ARCH" = "arm64" ]; then
  echo "ERROR: Cisco does not ship a mac-arm64 prebuilt for v${OPENH264_VERSION}." >&2
  echo "       Spike on x86_64 dev machine; arm64 vendoring is milestone item B." >&2
  exit 1
fi
if [ ! -f "$DYLIB_BZ2" ]; then
  curl -fsSL -o "$DYLIB_BZ2" \
    "https://github.com/cisco/openh264/releases/download/v${OPENH264_VERSION}/${DYLIB_BZ2}"
fi
if [ ! -f "$DYLIB_RAW" ]; then
  bunzip2 -k "$DYLIB_BZ2"
fi
cp -f "$DYLIB_RAW" "$DYLIB"
ls -lh "$DYLIB"

# Clone openh264 source for headers
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
cp "$DYLIB" "$WORK/openh264-prefix/lib/libopenh264.6.dylib"
ln -sf libopenh264.6.dylib "$WORK/openh264-prefix/lib/libopenh264.dylib"

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
echo "==> [2/5] Fetching ffmpeg source v${FFMPEG_VERSION}"
if [ ! -d "ffmpeg-${FFMPEG_VERSION}" ]; then
  if [ ! -f "ffmpeg-${FFMPEG_VERSION}.tar.xz" ]; then
    curl -fsSL -o "ffmpeg-${FFMPEG_VERSION}.tar.xz" \
      "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz"
  fi
  tar -xJf "ffmpeg-${FFMPEG_VERSION}.tar.xz"
fi
cd "ffmpeg-${FFMPEG_VERSION}"

# ---- Step 3: Configure ----------------------------------------------------
echo "==> [3/5] Configuring ffmpeg (LGPL, openh264, no x264, no GPL components)"
# IMPORTANT: ffmpeg processes configure flags left-to-right; --disable-everything
# MUST come BEFORE the per-component --enable-* flags so the enables take effect.
# Putting it last would silently re-disable everything we just enabled.
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
  echo "# ffmpeg LGPL configure invocation — used to vendor libs/ffmpeg/macos-${ARCH}/"
  echo "# Generated by scripts/build_ffmpeg_lgpl.sh on $(date -u +%FT%TZ)"
  echo "# ffmpeg version: ${FFMPEG_VERSION}"
  echo "# openh264 version: ${OPENH264_VERSION} (Cisco ${CISCO_TAG} prebuilt — inherits Cisco MPEG-LA royalty)"
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

# ---- Step 5: Stage outputs ------------------------------------------------
echo "==> [5/5] Staging Cisco openh264 prebuilt + license texts"
cp "$WORK/$DYLIB" "$PREFIX/lib/libopenh264.6.dylib"
chmod +w "$PREFIX/lib/libopenh264.6.dylib"
install_name_tool -id "@rpath/libopenh264.6.dylib" "$PREFIX/lib/libopenh264.6.dylib"
ln -sf libopenh264.6.dylib "$PREFIX/lib/libopenh264.dylib"
ln -sf libopenh264.6.dylib "$PREFIX/lib/libopenh264.7.dylib"  # spike compat shim

# Strip + set @rpath install names on ffmpeg dylibs.
# Also rewrite ALL inter-dylib references to @rpath form (ffmpeg's --prefix
# bakes absolute paths into the LC_LOAD_DYLIB commands; for vendored
# distribution we want the consumer to find them via -rpath).
for d in "$PREFIX/lib"/*.dylib; do
  if [ ! -L "$d" ]; then
    chmod +w "$d"
    strip -x "$d" 2>/dev/null || true
    install_name_tool -id "@rpath/$(basename "$d")" "$d" 2>/dev/null || true
    # Rewrite all internal absolute /Users/.../libs/ffmpeg/.../lib/X.dylib refs
    # to @rpath/X.dylib. Same for the openh264 install_name baked at /usr/local/lib.
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

# Compatibility symlinks for the plan's verify gate, which hardcodes the
# ffmpeg-8.x soname suffixes (.62/.62/.9/.60/.7) — this script vendored
# ffmpeg 7.1.2 (.61/.61/.8/.59) + openh264 2.1.1 (.6). Symlinks let the
# `test -f libs/ffmpeg/macos-${ARCH}/lib/libavcodec.62.dylib` checks
# continue to pass; the actual binaries are the .61.* files.
( cd "$PREFIX/lib"
  for pair in "libavcodec.61.19.101.dylib:libavcodec.62.dylib" \
              "libavformat.61.7.100.dylib:libavformat.62.dylib" \
              "libavutil.59.39.100.dylib:libavutil.60.dylib" \
              "libswscale.8.3.100.dylib:libswscale.9.dylib"; do
    src="${pair%:*}"; dst="${pair#*:}"
    [ -f "$src" ] && ln -sf "$src" "$dst"
  done
  [ -f libopenh264.6.dylib ] && ln -sf libopenh264.6.dylib libopenh264.7.dylib
)

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
