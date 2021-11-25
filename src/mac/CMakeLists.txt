#
# 86Box		A hypervisor and IBM PC system emulator that specializes in
#		running old operating systems and software designed for IBM
#		PC systems and compatibles from 1981 through fairly recent
#		system designs based on the PCI bus.
#
#		This file is part of the 86Box distribution.
#
#		CMake build script.
#
# Authors:	David Hrdlička, <hrdlickadavid@outlook.com>
#		dob205
#
#		Copyright 2020,2021 David Hrdlička.
#		Copyright 2021 dob205 J.Vernet.
#
# Prepare the macOS app bundle icon depending on the release channel

set(APP_ICON_MACOSX)
if (APPLE)
	if(RELEASE_BUILD)
		set(APP_ICON_MACOSX ${CMAKE_CURRENT_SOURCE_DIR}/mac/icons/release/86Box.icns)
	elseif(BETA_BUILD)
		set(APP_ICON_MACOSX ${CMAKE_CURRENT_SOURCE_DIR}/mac/icons/beta/86Box.icns)
	elseif(ALPHA_BUILD)
		set(APP_ICON_MACOSX ${CMAKE_CURRENT_SOURCE_DIR}/mac/icons/dev/86Box.icns)
	else()
		set(APP_ICON_MACOSX ${CMAKE_CURRENT_SOURCE_DIR}/mac/icons/branch/86Box.icns)
	endif()

set_source_files_properties(${APP_ICON_MACOSX} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
endif()

include_directories("./mac")
target_sources(86Box PUBLIC "./macOSXGlue.m")
target_link_libraries(86Box "-framework AppKit")

#some macOS specific configuration steps
# Force using the newest library if it's installed by homebrew
  set(CMAKE_FIND_FRAMEWORK LAST)
  

  # prepare stuff for macOS app bundles
  set(CMAKE_MACOSX_BUNDLE 1)

  # setting our compilation target to macOS 10.13 High Sierra
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13")
  
  # set the Info.plist properly
  set_target_properties(86Box PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in)
  set(MACOSX_BUNDLE_GUI_IDENTIFIER net.86Box.86Box)
  set(MACOSX_BUNDLE_BUNDLE_NAME 86Box)
  set(MACOSX_BUNDLE_BUNDLE_VERSION 3.0.${EMU_BUILD_NUM})
  set(MACOSX_BUNDLE_SHORT_VERSION_STRING "3.0.${EMU_BUILD_NUM}")
  set(MACOSX_BUNDLE_LONG_VERSION_STRING "3.0.${EMU_BUILD_NUM}")
  set(MACOSX_BUNDLE_ICON_FILE 86Box.icns)
  set(MACOSX_BUNDLE_INFO_STRING "A emulator of old computers")
  set(MACOSX_BUNDLE_COPYRIGHT "© 2007-${EMU_COPYRIGHT_YEAR} 86Box contributors")

  
  # preparing the code signing for easier distribution, Apple dev certificate needed at one point
  #set(XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "YES")
  #set(XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-")
  #set(XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS ${CMAKE_CURRENT_SOURCE_DIR}/mac/codesign/dev/app.entitlements)
  


  set(APPS ${CMAKE_CURRENT_BINARY_DIR}/86Box.app)
  install(CODE "
        	include(BundleUtilities)
        	fixup_bundle(\"${APPS}\" \"\" \"\")"
    		COMPONENT Runtime)

