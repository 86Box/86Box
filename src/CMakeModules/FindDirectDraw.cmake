# Locate directdraw
# This module defines
# DDRAW_LIBRARIES
# DDRAW_FOUND, if false, do not try to link to directinput
# DDRAW_INCLUDE_DIR, where to find the headers
#
# $DDRAW_DIR is an environment variable that would
# point to the this path in the plateform devkit (Samples\Multimedia\DirectShow)
#
# Created by Cedric Pinson.
#

SET( DDRAW_FOUND FALSE )

IF( WIN32 )
    FIND_PATH( DDRAW_ROOT_DIR Include/D3D10.h
               PATHS
               $ENV{PATH}
               $ENV{PROGRAMFILES}
    )
   
    FIND_PATH( DDRAW_INCLUDE_DIR ddraw.h
               PATHS
               ${DDRAW_ROOT_DIR}/Include
    )
   
    FIND_LIBRARY( DDRAW_LIBRARY ddraw.lib
                  PATHS
                  ${DDRAW_ROOT_DIR}/lib/x86
    )
   
    FIND_LIBRARY( DDRAW_GUID_LIBRARY dxguid.lib
                  PATHS
                  ${DDRAW_ROOT_DIR}/lib/x86
    )
   
    FIND_LIBRARY( DDRAW_ERR_LIBRARY dxerr.lib
                  PATHS
                  ${DDRAW_ROOT_DIR}/lib/x86
    )
   
    SET( DDRAW_LIBRARIES
         ${DDRAW_LIBRARY}
         ${DDRAW_GUID_LIBRARY}
         ${DDRAW_ERR_LIBRARY}
    )
   
    IF ( DDRAW_INCLUDE_DIR AND DDRAW_LIBRARIES )
        SET( DDRAW_FOUND TRUE )
    ENDIF ( DDRAW_INCLUDE_DIR AND DDRAW_LIBRARIES )
ENDIF( WIN32 )

MARK_AS_ADVANCED( DDRAW_FOUND )