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
#
#		Copyright 2020,2021 David Hrdlička.
#

cmake_minimum_required(VERSION 3.16)

cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(86Box
	VERSION 3.0
	DESCRIPTION "Emulator of x86-based systems"
	HOMEPAGE_URL "https://86box.github.io/"
	LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(TargetArch)
target_architecture(CMAKE_TARGET_ARCHITECTURES)

include(CPack)

include(CMakeDependentOption)

add_compile_definitions(CMAKE)

if(WIN32)
	# Disables *_s function warnings
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

	# Disables POSIX name warnings
	add_compile_definitions(_CRT_NONSTDC_NO_WARNINGS)

	# Disables WinSock deprecation warnings
	add_compile_definitions(_WINSOCK_DEPRECATED_NO_WARNINGS)
endif()

option(RELEASE "Release build" OFF)
option(USB "USB support" OFF)
option(DYNAREC "Dynamic recompiler" ON)
option(FLUIDSYNTH "FluidSynth" ON)
option(MUNT "MUNT" ON)
option(VRAMDUMP "Video RAM dumping" OFF)
option(DINPUT "DirectInput" OFF)
option(DISCORD "Discord integration" ON)

option(NEW_DYNAREC "Use the PCem v15 (\"new\") dynamic recompiler" OFF)

option(MINITRACE "Enable Chrome tracing using the modified minitrace library" OFF)

option(DEV_BRANCH "Development branch" OFF)
CMAKE_DEPENDENT_OPTION(AMD_K5 "AMD K5" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(CYRIX_6X86 "Cyrix 6x86" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(GUSMAX "Gravis UltraSound MAX" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(HEDAKA "Hedaka HED-919" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(I450KX "Intel i450KX" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(LASERXT "VTech Laser XT" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(MGA "Matrox Mystique graphics adapters" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(NO_SIO "Machines without emulated Super I/O chips" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(OLIVETTI "Olivetti M290" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(OPEN_AT "OpenAT" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(OPENGL "OpenGL 3.3 Core renderer" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(PAS16 "Pro Audio Spectrum 16" OFF "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(PS2M70T4 "IBM PS/2 model 70 (type 4)" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(S3TRIO3D2X "S3 Trio3D/2X" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(SIO_DETECT "Super I/O Detection Helper" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(M154X "ALi ALADDiN IV" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(M6117 "ALi M6117" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(VGAWONDER "ATI VGA Wonder (ATI-18800)" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(VNC "VNC renderer" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(XL24 "ATI VGA Wonder XL24 (ATI-28800-6)" ON "DEV_BRANCH" OFF)
CMAKE_DEPENDENT_OPTION(VECT486VL "HP Vectra 486VL" ON "DEV_BRANCH" OFF)

# HACK: Avoid a MSVC2019 compiler bug on ARM64 Debug builds
if(MSVC_TOOLSET_VERSION GREATER_EQUAL 142 AND CMAKE_TARGET_ARCHITECTURES STREQUAL "armv8")
	# Define a cache option in case somebody wants to disable this workaround
	set(AVOID_LNK1322 ON CACHE BOOL "Prevent LNK1322 on MSVC2019 ARM64 debug builds")

	if(AVOID_LNK1322)
		message(STATUS "Working around LNK1322 (#1268)")
		set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /Gy")
		set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Gy")
	endif()
endif()

add_subdirectory(src)
