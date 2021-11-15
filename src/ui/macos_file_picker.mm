#include "SDL_syswm.h"
#include "SDL_version.h"
#include <SDL.h>
#import <AppKit/AppKit.h>
#include <dispatch/dispatch.h>
#include <vector>
#include <string>
#include <utility>
#include <algorithm>
#include <tuple>
#include <iostream>
#include <sstream>

extern SDL_Window* sdl_win;
struct FileOpenSaveRequest
{
	std::vector<std::pair<std::string, std::string>>& filters;
	void (*filefunc3params)(uint8_t, char*, uint8_t) = nullptr;
	void (*filefunc2params)(uint8_t, char*) = nullptr;
	void (*filefunc2paramsalt)(char*, uint8_t) = nullptr;
	bool save = false;
	bool wp = false;
	uint8_t id = 0;
};

std::vector<std::string> split(const std::string& s)
{
    std::stringstream ss(s);
    std::vector<std::string> words;
    for (std::string w; ss>>w; ) words.push_back(w);
    return words;
}

void FileOpenSaveMacOS(FileOpenSaveRequest param)
{
    NSMutableArray<NSString*>* array = [[NSMutableArray alloc] init];
    bool wildcards = false;
    std::vector<std::string> extensions;
    NSWindow* wnd = nil;
    auto filterstr = param.filters[0].second;
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(sdl_win, &info))
    {
        wnd = info.info.cocoa.window;
    }
    extensions = split(filterstr);
            
    for (auto &ext : extensions)
    {
        size_t wildcardpos = std::string::npos;
        if (ext == "*.*") wildcards = true;
        while ((wildcardpos = ext.find('*')) != std::string::npos)
        {
            ext.erase(wildcardpos, 1);
        }
        while ((wildcardpos = ext.find('.')) != std::string::npos)
        {
            ext.erase(wildcardpos, 1);
        }
        if (ext.find('?') != std::string::npos) wildcards = true;
        auto nstr = [[NSString alloc] initWithCString:ext.c_str() encoding:NSASCIIStringEncoding];
        [array addObject:nstr];
        [array addObject:[nstr lowercaseString]];
    }
    if (param.save)
    {
        NSSavePanel* savePanel = [NSSavePanel savePanel];
        [savePanel setCanCreateDirectories:NO];
        [savePanel setAllowedFileTypes: wildcards ? nil : [NSArray arrayWithArray:array]];
        [savePanel setAllowsOtherFileTypes:YES];
        [array release];
        [savePanel beginSheetModalForWindow:wnd completionHandler:[param, savePanel, array](NSModalResponse result)
        {
            if (result == NSModalResponseOK)
            {
                const NSURL* url = [savePanel URL];
                const char* res = [[url path] UTF8String];
                if (param.filefunc3params) param.filefunc3params(param.id, (char*)res, param.wp);
	            else if (param.filefunc2params) param.filefunc2params(param.id, (char*)res);
	            else if (param.filefunc2paramsalt) param.filefunc2paramsalt((char*)res, param.wp);
            }
            else if (param.save && param.filefunc2paramsalt) param.filefunc2paramsalt(NULL, 0);
        }];
        //[savePanel setAllowedFileTypes:(API_DEPRECATED("Use -allowedContentTypes instead", macos(10.3,API_TO_BE_DEPRECATED)) NSArray<NSString *> *)]
    }
    else
    {
        NSOpenPanel* openPanel = [NSOpenPanel openPanel];
        [openPanel setCanChooseDirectories:NO];
        [openPanel setCanChooseFiles:YES];
        [openPanel setAllowsMultipleSelection:NO];
        [openPanel setAllowedFileTypes: wildcards ? nil : [NSArray arrayWithArray:array]];
        [array release];
        [openPanel beginSheetModalForWindow:wnd completionHandler:[param, openPanel, array](NSModalResponse result)
        {
            if (result == NSModalResponseOK)
            {
                const NSURL* url = [openPanel URL];
                const char* res = [[url path] UTF8String];
                if (param.filefunc3params) param.filefunc3params(param.id, (char*)res, param.wp);
	            else if (param.filefunc2params) param.filefunc2params(param.id, (char*)res);
	            else if (param.filefunc2paramsalt) param.filefunc2paramsalt((char*)res, param.wp);
            }
            else if (param.save && param.filefunc2paramsalt) param.filefunc2paramsalt(NULL, 0);
        }];
        //[savePanel setAllowedFileTypes:(API_DEPRECATED("Use -allowedContentTypes instead", macos(10.3,API_TO_BE_DEPRECATED)) NSArray<NSString *> *)]
    }
}
