#pragma once

namespace jamwide {

/// Returns true if the system default browser is Chromium-based.
/// BEST-EFFORT ADVISORY: Falls back to true (assume Chromium) if detection fails.
/// This is used to conditionally show a "Browser Compatibility" warning in the
/// privacy modal. It NEVER blocks video launch -- the user can always proceed
/// regardless of the detection result.
bool isDefaultBrowserChromium();

}  // namespace jamwide
