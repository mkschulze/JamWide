#!/usr/bin/env bash
# verify_camera_entitlement.sh - PKG-04 verification gate for Phase 19.
#
# Validates that a codesigned JamWide.app bundle on macOS carries BOTH:
#   1. The com.apple.security.device.camera entitlement (read from the signed
#      bundle via `codesign --display --entitlements -`, NOT from the source
#      .entitlements file — a tampered or stripped bundle MUST fail).
#   2. NSCameraUsageDescription matching the configured CAMERA_PERMISSION_TEXT
#      (read from the bundle's Info.plist via `plutil -extract` — the user-
#      visible TCC consent text).
#
# Optionally (with --notarize flag) verifies the Apple notarization staple
# via `xcrun stapler validate`.
#
# T-19-05 mitigation: source-only checks (reading JamWide.entitlements
# directly) would NOT catch a build that stripped the entitlement at
# codesign time. By going through `codesign --display --entitlements -` we
# pin the verification to the shipped artifact.
#
# Usage:
#   ./scripts/verify_camera_entitlement.sh <BUNDLE_PATH>             # entitlement + Info.plist
#   ./scripts/verify_camera_entitlement.sh <BUNDLE_PATH> --notarize  # + stapler validate
#
# Returns 0 on success; non-zero with a diagnostic naming the failed check.
# See .planning/phases/19-camera-capture-permission-ux/19-RESEARCH.md §7
# lines 654-670 for the verbatim command provenance.
set -euo pipefail

BUNDLE_PATH="${1:?Usage: $0 <BUNDLE_PATH> [--notarize]}"
NOTARIZE="${2:-}"
EXPECTED_USAGE='JamWide uses your webcam to share video with NINJAM peers.'

if [[ ! -d "$BUNDLE_PATH" ]]; then
    echo "FAIL: $BUNDLE_PATH not found or not a directory" >&2
    exit 1
fi

# Check 1: entitlement present in the signed bundle.
# `codesign --display --entitlements -` prints the entitlements XML to stdout
# (with a small header on stderr). We grep stdout for the camera key.
if ! codesign --display --entitlements - "$BUNDLE_PATH" 2>/dev/null \
        | grep -q 'com.apple.security.device.camera'; then
    echo "FAIL: com.apple.security.device.camera not found in bundle entitlements" >&2
    echo "      Hint: rebuild JamWide with JamWide.entitlements containing the" >&2
    echo "      camera key AND configure with -DJAMWIDE_HARDENED_RUNTIME=ON so" >&2
    echo "      the codesign step actually applies the .entitlements file." >&2
    exit 2
fi

# Check 2: NSCameraUsageDescription is present and matches the expected text.
# `plutil -extract <key> raw` prints the key's value (or fails if missing).
if ! ACTUAL=$(plutil -extract NSCameraUsageDescription raw "$BUNDLE_PATH/Contents/Info.plist" 2>/dev/null); then
    echo "FAIL: NSCameraUsageDescription missing from Info.plist" >&2
    echo "      Hint: CMakeLists.txt must pass" >&2
    echo "            CAMERA_PERMISSION_ENABLED TRUE" >&2
    echo "            CAMERA_PERMISSION_TEXT \"$EXPECTED_USAGE\"" >&2
    echo "      to juce_add_plugin so JUCE emits the Info.plist key at bundle time." >&2
    exit 3
fi
if [[ "$ACTUAL" != "$EXPECTED_USAGE" ]]; then
    echo "FAIL: NSCameraUsageDescription mismatch" >&2
    echo "      Expected: $EXPECTED_USAGE" >&2
    echo "      Actual:   $ACTUAL" >&2
    exit 4
fi

# Check 3 (optional): notarization staple is valid.
# Only meaningful for a release build that has been through `notarytool submit`
# and `stapler staple`. Skip by default to keep dev builds passing.
if [[ "$NOTARIZE" == "--notarize" ]]; then
    if ! xcrun stapler validate "$BUNDLE_PATH"; then
        echo "FAIL: stapler validation failed (bundle not notarized or staple missing)" >&2
        exit 5
    fi
fi

echo "OK: $BUNDLE_PATH passes PKG-04 entitlement + NSCameraUsageDescription verification"
if [[ "$NOTARIZE" == "--notarize" ]]; then
    echo "OK: notarization staple is valid"
fi
exit 0
