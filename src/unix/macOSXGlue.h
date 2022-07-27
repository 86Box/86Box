//
//  macOSXGlue.h
//  TestSDL
//
//  Created by Jerome Vernet on 18/11/2021.
//  Copyright © 2021 Jerome Vernet. All rights reserved.
//

#ifndef macOSXGlue_h
#define macOSXGlue_h

#include <CoreFoundation/CFBase.h>

CF_IMPLICIT_BRIDGING_ENABLED
CF_EXTERN_C_BEGIN
void getDefaultROMPath(char*);
int toto();

CF_EXTERN_C_END
CF_IMPLICIT_BRIDGING_DISABLED


#endif /* macOSXGlue_h */
