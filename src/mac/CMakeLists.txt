#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script.
#
# Authors: dob205,
#          Jerome Vernet
#          David Hrdlička, <hrdlickadavid@outlook.com>
#
#          Copyright 2021 dob205.
#          Copyright 2021 Jerome Vernet.
#          Copyright 2021 David Hrdlička.
#

# Pick the bundle icon depending on the release channel
if(RELEASE_BUILD)
    set(APP_ICON_MACOSX icons/release/86Box.icns)
elseif(BETA_BUILD)
    set(APP_ICON_MACOSX icons/beta/86Box.icns)
elseif(ALPHA_BUILD)
    set(APP_ICON_MACOSX icons/dev/86Box.icns)
else()
    set(APP_ICON_MACOSX icons/branch/86Box.icns)
endif()

target_link_libraries(86Box "-framework AppKit")
target_sources(86Box PRIVATE ${APP_ICON_MACOSX})

# Make sure the icon is copied to the bundle
set_source_files_properties(${APP_ICON_MACOSX}
    TARGET_DIRECTORY 86Box
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

# Prepare long version string
if(EMU_BUILD)
    set(LONG_VER_STRING "${CMAKE_PROJECT_VERSION} [${EMU_BUILD}]")
else()
    set(LONG_VER_STRING "${CMAKE_PROJECT_VERSION}")
endif()

# Generate Info.plist
configure_file(Info.plist.in Info.plist @ONLY)
set_target_properties(86Box
    PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist)

#set(XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "YES")
#set(XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-")
#set(XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS ${CMAKE_CURRENT_SOURCE_DIR}/mac/codesign/dev/app.entitlements)
