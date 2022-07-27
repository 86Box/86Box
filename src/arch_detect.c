/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Configure-time architecture detection for the CMake build.
 *
 *
 *
 * Authors:	David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *		Copyright 2020-2021 David Hrdlička.
 */

#if defined(__arm__) || defined(__TARGET_ARCH_ARM)
    #error ARCH arm
#elif defined(__aarch64__) || defined(_M_ARM64)
    #error ARCH arm64
#elif defined(__i386) || defined(__i386__) || defined(_M_IX86)
    #error ARCH i386
#elif defined(__x86_64) || defined(__x86_64__) || defined(__amd64) || defined(_M_X64)
    #error ARCH x86_64
#endif
#error ARCH unknown
