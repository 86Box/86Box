//
//  macOSXGlue.m
//  86BOx MacoSx Glue....
// Todo: so much
//  Created by Jerome Vernet on 18/11/2021.
//  Copyright © 2021 Jerome Vernet. All rights reserved.
//

#import <Foundation/Foundation.h>

void getDefaultROMPath(char* Path)
{
	NSFileManager* sharedFM = [NSFileManager defaultManager];
	NSArray* possibleURLs = [sharedFM URLsForDirectory:NSApplicationSupportDirectory
											 inDomains:NSUserDomainMask];
	NSURL* appSupportDir = nil;
	NSURL* appDirectory = nil;
	
	if ([possibleURLs count] >= 1) {
		// Use the first directory (if multiple are returned)
		appSupportDir = [possibleURLs objectAtIndex:0];
	}
	
	// If a valid app support directory exists, add the
	// app's bundle ID to it to specify the final directory.
	if (appSupportDir) {
		NSString* appBundleID = [[NSBundle mainBundle] bundleIdentifier];
		appDirectory = [appSupportDir URLByAppendingPathComponent:appBundleID];
		appDirectory=[appDirectory URLByAppendingPathComponent:@"roms"];
	}
    // create ~/Library/Application Support/... stuff
	
	NSError*    theError = nil;
	if (![sharedFM createDirectoryAtURL:appDirectory withIntermediateDirectories:YES
					   attributes:nil error:&theError])
	{
		// Handle the error.
		NSLog(@"Error creating user library rom path");
	} else NSLog(@"Create user rom path sucessfull");
	
	strcpy(Path,[appDirectory fileSystemRepresentation]);
	// return appDirectory;
}
