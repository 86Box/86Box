//
//  macOSXPrefs.m
//  
//
//  Created by Jerome Vernet on 19/11/2021.
//

#import <Foundation/Foundation.h>
#include <Cocoa/Cocoa.h>

// The _UINT64 define is needed to guard against a typedef mismatch with Snow Leopard headers.
#define _UINT64


#include "macOSXPrefs.h"

@interface 86BoxMain : NSObject
{
	NSArray *nibObjects;
	NSWindow *prefsWindow;
}
@end

@implementation 86BoxMain


- (NSArray*) loadPrefsNibFile
{
	NSNib *nib = [[NSNib alloc] initWithNibNamed:@"VMSettingsWindow" bundle:nil];
	NSArray *objects = nil;
	
	if (![nib instantiateNibWithOwner:[VMSettingsController sharedInstance] topLevelObjects:&objects]) {
		NSLog(@"Could not load Prefs NIB file!\n");
		return nil;
	}
	
	NSLog(@"%d objects loaded\n", [objects count]);
	
	// Release the raw nib data.
	[nib release];
	
	// Release the top-level objects so that they are just owned by the array.
	[objects makeObjectsPerformSelector:@selector(release)];
	
	prefsWindow = nil;
	for (int i = 0; i < [objects count]; i++) {
		NSObject *object = [objects objectAtIndex:i];
		NSLog(@"Got %@", object);
		
		if ([object isKindOfClass:[NSWindow class]]) {
			prefsWindow = (NSWindow *) object;
			break;
		}
	}
	
	if (prefsWindow == nil) {
		NSLog(@"Could not find NSWindow in Prefs NIB file!\n");
		return nil;
	}
	
	return objects;
}


- (void) openPreferences:(id)sender
{
	NSAutoreleasePool *pool;
	
	if (nibObjects == nil) {
		nibObjects = [self loadPrefsNibFile];
		if (nibObjects == nil)
			return;
		[nibObjects retain];
	}
	
	pool = [[NSAutoreleasePool alloc] init];
	[[VMSettingsController sharedInstance] setupGUI];
	[NSApp runModalForWindow:prefsWindow];
	[pool release];
}

- (BOOL)validateMenuItem:(NSMenuItem *)menuItem
{
	return YES;
}

@end

/*
 *  Initialization
 */

void macOSXprefs_init(void)
{
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	for (NSMenuItem *sub_item in [NSApp mainMenu].itemArray[0].submenu.itemArray) {
		if ([sub_item.title isEqualToString:@"Preferences…"]) {
			sub_item.target = [[86BoxMain alloc] init];
			sub_item.action = @selector(openPreferences:);
			break;
		}
	}
	[pool release];
}

/*
 *  Deinitialization
 */

void macosXprefs_exit(void)
{
}
