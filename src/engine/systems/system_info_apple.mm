#include "system_info.hpp"

#import <Foundation/Foundation.h>
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#endif

namespace dbc
{
    std::string AppleDefaultWebBrowser()
    {
#if TARGET_OS_OSX
        @autoreleasepool
        {
            NSURL* Page = [NSURL URLWithString:@"http://example.com"];
            NSURL* Browser = [[NSWorkspace sharedWorkspace] URLForApplicationToOpenURL:Page];
            if (Browser == nil)
                return {};
            NSString* Name = [[NSFileManager defaultManager] displayNameAtPath:Browser.path];
            if ([Name hasSuffix:@".app"])
                Name = [Name stringByDeletingPathExtension];
            return Name.UTF8String ? Name.UTF8String : "";
        }
#else
        // iOS, iPadOS and visionOS do not tell applications which browser is the default.
        return {};
#endif
    }

} // namespace dbc
