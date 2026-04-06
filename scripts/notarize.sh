#!/usr/bin/env bash
#
# Sign, notarize, and staple JamWide macOS plugin bundles.
#
# Usage:
#   ./scripts/notarize.sh [build-dir]
#
# Defaults:
#   build-dir = build-juce/JamWideJuce_artefacts/Release
#
# Prerequisites:
#   - Developer ID Application certificate in Keychain
#   - ~/.appstoreconnect/notarize.env with NOTARIZE_KEY_ID, NOTARIZE_ISSUER_ID, NOTARIZE_KEY_PATH
#     OR environment variables already set (for CI)
#
set -euo pipefail

# --- Configuration -----------------------------------------------------------

TEAM_ID="T3KK66Q67T"
SIGN_IDENTITY="Developer ID Application: Mark-Kristopher Schulze ($TEAM_ID)"
ENTITLEMENTS="$(cd "$(dirname "$0")/.." && pwd)/JamWide.entitlements"
BUILD_DIR="${1:-build-juce/JamWideJuce_artefacts/Release}"

# Load credentials from env file if not already set
if [[ -z "${NOTARIZE_KEY_ID:-}" ]]; then
    ENV_FILE="${HOME}/.appstoreconnect/notarize.env"
    if [[ -f "$ENV_FILE" ]]; then
        # shellcheck source=/dev/null
        source "$ENV_FILE"
    else
        echo "ERROR: No notarization credentials found."
        echo "Set NOTARIZE_KEY_ID, NOTARIZE_ISSUER_ID, NOTARIZE_KEY_PATH"
        echo "or create ~/.appstoreconnect/notarize.env"
        exit 1
    fi
fi

KEY_PATH=$(eval echo "${NOTARIZE_KEY_PATH}")

if [[ ! -f "$KEY_PATH" ]]; then
    echo "ERROR: API key not found at $KEY_PATH"
    exit 1
fi

# --- Helpers ------------------------------------------------------------------

sign_bundle() {
    local bundle="$1"
    local name
    name=$(basename "$bundle")

    if [[ ! -e "$bundle" ]]; then
        echo "  SKIP $name (not found)"
        return
    fi

    echo "  SIGN $name"
    codesign --force --deep --options runtime \
        --sign "$SIGN_IDENTITY" \
        --entitlements "$ENTITLEMENTS" \
        --timestamp \
        "$bundle"

    echo "  VERIFY $name"
    codesign --verify --deep --strict "$bundle"
}

notarize_zip() {
    local zip_path="$1"
    local name
    name=$(basename "$zip_path")

    echo "  SUBMIT $name"
    xcrun notarytool submit "$zip_path" \
        --key "$KEY_PATH" \
        --key-id "$NOTARIZE_KEY_ID" \
        --issuer "$NOTARIZE_ISSUER_ID" \
        --wait
}

staple_bundle() {
    local bundle="$1"
    local name
    name=$(basename "$bundle")

    if [[ ! -e "$bundle" ]]; then
        return
    fi

    echo "  STAPLE $name"
    xcrun stapler staple "$bundle"
}

# --- Main ---------------------------------------------------------------------

echo "=== JamWide Code Signing ==="
echo "Identity: $SIGN_IDENTITY"
echo "Build dir: $BUILD_DIR"
echo ""

# Collect bundles
BUNDLES=()
for fmt in VST3/JamWide.vst3 AU/JamWide.component CLAP/JamWide.clap Standalone/JamWide.app; do
    path="$BUILD_DIR/$fmt"
    if [[ -e "$path" ]]; then
        BUNDLES+=("$path")
    fi
done

if [[ ${#BUNDLES[@]} -eq 0 ]]; then
    echo "ERROR: No bundles found in $BUILD_DIR"
    echo "Expected: VST3/JamWide.vst3, AU/JamWide.component, etc."
    exit 1
fi

# Step 1: Sign all bundles
echo "--- Signing ---"
for bundle in "${BUNDLES[@]}"; do
    sign_bundle "$bundle"
done
echo ""

# Step 2: Create zip for notarization (all bundles in one submission)
echo "--- Packaging for notarization ---"
NOTARIZE_ZIP="$BUILD_DIR/JamWide-notarize.zip"
STAGING_DIR=$(mktemp -d)
for bundle in "${BUNDLES[@]}"; do
    cp -R "$bundle" "$STAGING_DIR/"
done
ditto -c -k --keepParent "$STAGING_DIR" "$NOTARIZE_ZIP"
rm -rf "$STAGING_DIR"
echo "  Created $NOTARIZE_ZIP ($(du -h "$NOTARIZE_ZIP" | cut -f1))"
echo ""

# Step 3: Notarize
echo "--- Notarizing ---"
notarize_zip "$NOTARIZE_ZIP"
echo ""

# Step 4: Staple all bundles
echo "--- Stapling ---"
for bundle in "${BUNDLES[@]}"; do
    staple_bundle "$bundle"
done
echo ""

# Cleanup
rm -f "$NOTARIZE_ZIP"

echo "=== Done ==="
echo "Signed and notarized:"
for bundle in "${BUNDLES[@]}"; do
    echo "  $(basename "$bundle")"
done
