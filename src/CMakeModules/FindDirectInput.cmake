# Locate directinput
# This module defines
# DIRECTINPUT_LIBRARIES
# DIRECTINPUT_FOUND, if false, do not try to link to directinput
# DIRECTINPUT_INCLUDE_DIR, where to find the headers
#
# $DIRECTINPUT_DIR is an environment variable that would
# point to the this path in the plateform devkit (Samples\Multimedia\DirectShow)
#
# Created by Cedric Pinson.
#

SET( DIRECTINPUT_FOUND FALSE )

IF( WIN32 )
    FIND_PATH( DIRECTINPUT_ROOT_DIR Include/D3D10.h
               PATHS
               $ENV{PATH}
               $ENV{PROGRAMFILES}
    )
   
    FIND_PATH( DIRECTINPUT_INCLUDE_DIR dinput.h
               PATHS
               ${DIRECTINPUT_ROOT_DIR}/Include
    )
   
    FIND_LIBRARY( DIRECTINPUT_LIBRARY dinput7.lib dinput8.lib
                  PATHS
                  ${DIRECTINPUT_ROOT_DIR}/lib/x86
    )
   
    FIND_LIBRARY( DIRECTINPUT_GUID_LIBRARY dxguid.lib
                  PATHS
                  ${DIRECTINPUT_ROOT_DIR}/lib/x86
    )
   
    FIND_LIBRARY( DIRECTINPUT_ERR_LIBRARY dxerr.lib
                  PATHS
                  ${DIRECTINPUT_ROOT_DIR}/lib/x86
    )
   
    SET( DIRECTINPUT_LIBRARIES
         ${DIRECTINPUT_LIBRARY}
         ${DIRECTINPUT_GUID_LIBRARY}
         ${DIRECTINPUT_ERR_LIBRARY}
    )
   
    IF ( DIRECTINPUT_INCLUDE_DIR AND DIRECTINPUT_LIBRARIES )
        SET( DIRECTINPUT_FOUND TRUE )
    ENDIF ( DIRECTINPUT_INCLUDE_DIR AND DIRECTINPUT_LIBRARIES )
ENDIF( WIN32 )

MARK_AS_ADVANCED( DIRECTINPUT_FOUND )