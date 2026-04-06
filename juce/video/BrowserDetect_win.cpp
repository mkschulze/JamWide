#include "BrowserDetect.h"

#if JUCE_WINDOWS
#include <windows.h>
#include <string>

namespace jamwide {

bool isDefaultBrowserChromium()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\https\\UserChoice",
        0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return true;  // Best-effort: assume Chromium on failure

    wchar_t progId[256];
    DWORD size = sizeof(progId);
    bool result = true;  // Default: assume Chromium
    if (RegQueryValueExW(hKey, L"ProgId", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(progId), &size) == ERROR_SUCCESS)
    {
        std::wstring id(progId);
        result = (id.find(L"ChromeHTML") != std::wstring::npos ||
                  id.find(L"MSEdgeHTM") != std::wstring::npos ||
                  id.find(L"BraveHTML") != std::wstring::npos ||
                  id.find(L"VivaldiHTM") != std::wstring::npos ||
                  id.find(L"OperaStable") != std::wstring::npos);
    }
    RegCloseKey(hKey);
    return result;
}

}  // namespace jamwide

#elif !JUCE_MAC
// Linux and other platforms: assume Chromium (best-effort, skip warning)
namespace jamwide {
bool isDefaultBrowserChromium() { return true; }
}
#endif
