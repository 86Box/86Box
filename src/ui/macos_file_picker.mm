#import <AppKit/AppKit.h>
#include <vector>
#include <string>
#include <utility>
#include <algorithm>
#include <tuple>

bool OpenFileChooserMacos(char* res, size_t n, std::vector<std::pair<std::string, std::string>>& filters, bool save = false)
{
    
    if (save)
    {
        @autoreleasepool
        {
            NSSavePanel* savePanel = [NSSavePanel savePanel];
            [savePanel canCreateDirectories:NO];
            //[savePanel setAllowedFileTypes:(API_DEPRECATED("Use -allowedContentTypes instead", macos(10.3,API_TO_BE_DEPRECATED)) NSArray<NSString *> *)]
        }
    }
}