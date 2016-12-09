# Locate directdraw
# This module defines
# D3D9_LIBRARIES
# D3D9_FOUND, if false, do not try to link to directinput
# D3D9_INCLUDE_DIR, where to find the headers
#
# $D3D9_DIR is an environment variable that would
# point to the this path in the plateform devkit (Samples\Multimedia\DirectShow)
#
# Created by Cedric Pinson.
#

SET( D3D9_FOUND FALSE )

IF( WIN32 )
    FIND_PATH( D3D9_ROOT_DIR Include/D3D9.h
               PATHS
               $ENV{PATH}
               $ENV{PROGRAMFILES}
    )
   
    FIND_PATH( D3D9_INCLUDE_DIR d3d9.h
               PATHS
               ${D3D9_ROOT_DIR}/Include
    )
   
    FIND_LIBRARY( D3D9_LIBRARY d3d9.lib d3dx9
                  PATHS
                  ${D3D9_ROOT_DIR}/lib/x86
    )
   
    FIND_LIBRARY( D3D9_GUID_LIBRARY dxguid.lib
                  PATHS
                  ${D3D9_ROOT_DIR}/lib/x86
    )
   
    FIND_LIBRARY( D3D9_ERR_LIBRARY dxerr.lib
                  PATHS
                  ${D3D9_ROOT_DIR}/lib/x86
    )
   
    SET( D3D9_LIBRARIES
         ${D3D9_LIBRARY}
         ${D3D9_GUID_LIBRARY}
         ${D3D9_ERR_LIBRARY}
    )
   
    IF ( D3D9_INCLUDE_DIR AND D3D9_LIBRARIES )
        SET( D3D9_FOUND TRUE )
    ENDIF ( D3D9_INCLUDE_DIR AND D3D9_LIBRARIES )
ENDIF( WIN32 )

MARK_AS_ADVANCED( D3D9_FOUND )