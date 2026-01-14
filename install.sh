#!/bin/bash
# Build and install JamWide plugin

set -e

cd "$(dirname "$0")"

# Increment build number
BUILD_FILE="src/build_number.h"
if [ -f "$BUILD_FILE" ]; then
    CURRENT=$(grep JAMWIDE_BUILD_NUMBER "$BUILD_FILE" | grep -o '[0-9]*')
    NEW=$((CURRENT + 1))
    echo "#pragma once" > "$BUILD_FILE"
    echo "#define JAMWIDE_BUILD_NUMBER $NEW" >> "$BUILD_FILE"
    echo "Build number: r$NEW"
fi

# Build
cmake --build build

# Install locations (user)
CLAP_DIR="$HOME/Library/Audio/Plug-Ins/CLAP"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"

# Create directories
mkdir -p "$CLAP_DIR" "$VST3_DIR" "$AU_DIR"

# Install CLAP
rm -rf "$CLAP_DIR/JamWide.clap"
cp -R build/JamWide.clap "$CLAP_DIR/"
SetFile -a B "$CLAP_DIR/JamWide.clap"
echo "Installed JamWide.clap to $CLAP_DIR"

# Install VST3
rm -rf "$VST3_DIR/JamWide.vst3"
cp -R build/JamWide.vst3 "$VST3_DIR/"
SetFile -a B "$VST3_DIR/JamWide.vst3"
echo "Installed JamWide.vst3 to $VST3_DIR"

# Install AU
rm -rf "$AU_DIR/JamWide.component"
cp -R build/JamWide.component "$AU_DIR/"
SetFile -a B "$AU_DIR/JamWide.component"
echo "Installed JamWide.component to $AU_DIR"

echo ""
echo "JamWide r$NEW installed (CLAP, VST3, AU)"
