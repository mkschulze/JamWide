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

# Install location (user)
INSTALL_DIR="$HOME/Library/Audio/Plug-Ins/CLAP"
TARGET="$INSTALL_DIR/JamWide.clap"

# Remove old and install new
mkdir -p "$INSTALL_DIR"
rm -rf "$TARGET"
cp -R build/JamWide.clap "$INSTALL_DIR/"
SetFile -a B "$TARGET"

echo "Installed JamWide.clap (r$NEW) to $INSTALL_DIR"
