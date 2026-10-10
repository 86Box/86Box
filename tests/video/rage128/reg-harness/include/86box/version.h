/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- test harness stand-in for the version
 *          header.
 *
 *          The emulator build generates 86box/version.h with CMake, and
 *          the device stamps its telemetry header with the build version
 *          from it. The harnesses' build.sh scripts have no CMake tree to
 *          take it from, and the CMake build of the harnesses uses this
 *          file too, so both builds stamp the same text.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#ifndef EMU_VERSION_H
#define EMU_VERSION_H

#define EMU_VERSION      "harness"
#define EMU_VERSION_FULL "harness"

#endif
