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
#		Copyright 2021 dob205.
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

# WIN32 marks us as a GUI app on Windows
# MACOSX_BUNDLE prepares a macOS application bundle including with the app icon
add_executable(86Box WIN32 MACOSX_BUNDLE 86box.c config.c log.c random.c timer.c io.c acpi.c apm.c
	dma.c ddma.c nmi.c pic.c pit.c port_6x.c port_92.c ppi.c pci.c mca.c usb.c
	device.c nvr.c nvr_at.c nvr_ps2.c rtmidi_midi.cpp ${APP_ICON_MACOSX})

if(APPLE)
	target_link_libraries(86Box "-framework AppKit")
endif()

if(NEW_DYNAREC)
	add_compile_definitions(USE_NEW_DYNAREC)
endif()

if(RELEASE)
	add_compile_definitions(RELEASE_BUILD)
endif()

if(DYNAREC)
	add_compile_definitions(USE_DYNAREC)
endif()

if(VRAMDUMP)
	add_compile_definitions(ENABLE_VRAM_DUMP)
endif()

if(DEV_BRANCH)
	add_compile_definitions(DEV_BRANCH)
endif()

if(VNC)
	add_compile_definitions(USE_VNC)
	add_library(vnc OBJECT vnc.c vnc_keymap.c)
	target_link_libraries(86Box vnc vncserver)
	if (WIN32)
		target_link_libraries(86Box ws2_32)
	endif()
endif()

target_link_libraries(86Box cpu chipset mch dev mem fdd game cdrom zip mo hdd
	net print scsi sio snd vid voodoo plat ui)

if(WIN32 AND ARCH STREQUAL "i386")
	if(MSVC)
		target_link_options(86Box PRIVATE "/LARGEADDRESSAWARE")
	else()
		target_link_options(86Box PRIVATE "LINKER:--large-address-aware")
	endif()
endif()

if(MINGW)
	target_link_options(86Box PRIVATE "-static")
	set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".dll.a")
endif()

# Variables introduced by richardg867 for versioning stuff
if(NOT CMAKE_PROJECT_VERSION_PATCH)
	set(CMAKE_PROJECT_VERSION_PATCH 0)
endif()
if(NOT EMU_BUILD_NUM)
	set(EMU_BUILD_NUM 0)
endif()
if(NOT EMU_COPYRIGHT_YEAR)
	set(EMU_COPYRIGHT_YEAR 2021)
endif()

#some macOS specific configuration steps
if(APPLE)
# Force using the newest library if it's installed by homebrew
  set(CMAKE_FIND_FRAMEWORK LAST)
  

  # prepare stuff for macOS app bundles
  set(CMAKE_MACOSX_BUNDLE 1)

  # setting our compilation target to macOS 10.13 High Sierra
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13")
  
  # set the Info.plist properly
  set_target_properties(86Box PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist.in)
  set(MACOSX_BUNDLE_GUI_IDENTIFIER net.86Box.86Box)
  set(MACOSX_BUNDLE_BUNDLE_NAME 86Box)
  set(MACOSX_BUNDLE_BUNDLE_VERSION 3.1.${EMU_BUILD_NUM})
  set(MACOSX_BUNDLE_SHORT_VERSION_STRING "3.1.${EMU_BUILD_NUM}")
  set(MACOSX_BUNDLE_LONG_VERSION_STRING "3.1.${EMU_BUILD_NUM}")
  set(MACOSX_BUNDLE_ICON_FILE 86Box.icns)
  set(MACOSX_BUNDLE_INFO_STRING "A emulator of old computers")
  set(MACOSX_BUNDLE_COPYRIGHT "© 2007-${EMU_COPYRIGHT_YEAR} 86Box contributors")

  
  # preparing the code signing for easier distribution, Apple dev certificate needed at one point
  #set(XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "YES")
  #set(XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-")
  #set(XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS ${CMAKE_CURRENT_SOURCE_DIR}/mac/codesign/dev/app.entitlements)
  
endif()

if(QT)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  
  # if we want to use qt5-static to eliminate need for bundling qt dlls (windows)
  #set(QT_STATIC ON)
  # needed to build with qt5-static if both qt5 and qt5-static are installed
  #set(CMAKE_PREFIX_PATH "path/to/qt5-static")
  
  find_package(Qt5 COMPONENTS Core Widgets REQUIRED)
endif()

find_package(Freetype REQUIRED)
include_directories(${FREETYPE_INCLUDE_DIRS})
if(APPLE)
	target_link_libraries(86Box Freetype::Freetype) # bundles freetype for the macOS app bundle
endif()

find_package(OpenAL REQUIRED)
include_directories(${OPENAL_INCLUDE_DIR})
target_link_libraries(86Box ${OPENAL_LIBRARY})

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})
if(MINGW)
	target_link_libraries(86Box SDL2::SDL2-static)
elseif(WIN32)
	target_link_libraries(86Box SDL2::SDL2)
else()
        if (TARGET SDL2::SDL2)
                target_link_libraries(86Box SDL2::SDL2)
        else()
	        target_link_libraries(86Box ${SDL2_LIBRARIES})
        endif()
endif()

find_package(PNG REQUIRED)
include_directories(${PNG_INCLUDE_DIRS})
target_link_libraries(86Box PNG::PNG)

if(VCPKG_TOOLCHAIN)
	# vcpkg includes a config file for rtmidi
	find_package(RtMidi REQUIRED)
	target_link_libraries(86Box RtMidi::rtmidi)
else()
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(RTMIDI REQUIRED IMPORTED_TARGET rtmidi)
	target_link_libraries(86Box PkgConfig::RTMIDI)

	if(WIN32)
		target_link_libraries(PkgConfig::RTMIDI INTERFACE winmm)
	endif()
endif()

configure_file(include/86box/version.h.in include/86box/version.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)

include_directories(include)
if(NEW_DYNAREC)
	include_directories(cpu codegen_new)
else()
	include_directories(cpu codegen)
endif()

add_subdirectory(cdrom)
add_subdirectory(chipset)

add_subdirectory(cpu)
if(NEW_DYNAREC)
	add_subdirectory(codegen_new)
else()
	add_subdirectory(codegen)
endif()

if(MINITRACE)
    add_compile_definitions(MTR_ENABLED)
    add_library(minitrace OBJECT minitrace/minitrace.c)
    target_link_libraries(86Box minitrace)
endif()

if(APPLE)
	install(TARGETS 86Box DESTINATION "bin")
else()
	install(TARGETS 86Box)
endif()

# adjustments for macOS app bundles
if(APPLE)
	set(APPS ${CMAKE_CURRENT_BINARY_DIR}/86Box.app)
	install(CODE "
		include(InstallRequiredSystemLibraries)
        	include(BundleUtilities)
        	fixup_bundle(\"${APPS}\" \"\" \"\")"
    	COMPONENT Runtime)

	# needed for Qt packaging
	# get the macdeployqt path
	get_target_property(_qmake_executable Qt5::qmake IMPORTED_LOCATION)
	get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
	find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${_qt_bin_dir}")

	# excecute macdeployqt
	add_custom_command(TARGET 86Box POST_BUILD
		COMMAND "${MACDEPLOYQT_EXECUTABLE}"
			"$<TARGET_FILE_DIR:86Box>/../.."
			-always-overwrite
		COMMENT "Running macdeployqt..."
		)
endif()

if(VCPKG_TOOLCHAIN)
	x_vcpkg_install_local_dependencies(TARGETS 86Box DESTINATION "bin")
endif()

if(MSVC)
	install(FILES $<TARGET_PDB_FILE:86Box>
		CONFIGURATIONS Debug RelWithDebInfo
		DESTINATION "bin")
endif()

add_subdirectory(device)
add_subdirectory(disk)
add_subdirectory(floppy)
add_subdirectory(game)
add_subdirectory(machine)
add_subdirectory(mem)
add_subdirectory(network)
add_subdirectory(printer)
add_subdirectory(sio)
add_subdirectory(scsi)
add_subdirectory(sound)
add_subdirectory(video)
if (APPLE)
	add_subdirectory(mac)
endif()

if (QT)
        add_subdirectory(qt)
elseif(WIN32)
	add_subdirectory(win)
else()
	add_subdirectory(unix)
endif()
