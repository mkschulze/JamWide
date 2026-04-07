#include "BrowserDetect.h"

#import <CoreServices/CoreServices.h>
#import <Foundation/Foundation.h>

namespace jamwide {

bool isDefaultBrowserChromium()
{
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CFStringRef bundleId = LSCopyDefaultHandlerForURLScheme(CFSTR("https"));
    #pragma clang diagnostic pop

    if (!bundleId) return true;  // Best-effort: assume Chromium on failure (skip warning)

    NSString* bid = (__bridge_transfer NSString*)bundleId;
    NSArray* chromiumIds = @[
        @"com.google.Chrome",
        @"com.google.Chrome.canary",
        @"com.microsoft.edgemac",
        @"com.brave.Browser",
        @"com.vivaldi.Vivaldi",
        @"com.opera.Opera",
        @"company.thebrowser.Browser",
        @"org.chromium.Chromium"
    ];
    for (NSString* cid in chromiumIds) {
        if ([bid isEqualToString:cid]) return true;
    }
    return false;
}

}  // namespace jamwide
