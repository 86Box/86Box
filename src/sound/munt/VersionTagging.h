/* Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Dean Beeler, Jerome Fisher
 * Copyright (C) 2011-2022 Dean Beeler, Jerome Fisher, Sergey V. Mikayev
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MT32EMU_VERSION_TAG_H
#define MT32EMU_VERSION_TAG_H

#include "globals.h"

/* This is intended to implement a simple check of a shared library version in runtime. Sadly, per-symbol versioning
 * is unavailable on many platforms, and even where it is, it's still not too easy to maintain for a C++ library.
 * Therefore, the goal here is just to ensure that the client application quickly bails out when attempted to run
 * with an older version of shared library, as well as to produce a more readable error message indicating a version mismatch
 * rather than a report about some missing symbols with unreadable mangled names.
 * This is an optional feature, since it adds some minor burden to both the library and client applications code,
 * albeit it is ought to work on platforms that do not implement symbol versioning.
 */

#define MT32EMU_REALLY_BUILD_VERSION_TAG(major, minor) mt32emu_ ## major ## _ ## minor
/* This macro expansion step permits resolution the actual version numbers. */
#define MT32EMU_BUILD_VERSION_TAG(major, minor) MT32EMU_REALLY_BUILD_VERSION_TAG(major, minor)
#define MT32EMU_VERSION_TAG MT32EMU_BUILD_VERSION_TAG(MT32EMU_VERSION_MAJOR, MT32EMU_VERSION_MINOR)

#if defined(__cplusplus)

extern "C" {
MT32EMU_EXPORT extern const volatile char MT32EMU_VERSION_TAG;
}
// This pulls the external reference in yet prevents it from being optimised out.
static const volatile char mt32emu_version_tag = MT32EMU_VERSION_TAG;

#else

static void mt32emu_refer_version_tag(void) {
	MT32EMU_EXPORT extern const volatile char MT32EMU_VERSION_TAG;
	(void)MT32EMU_VERSION_TAG;
}

static void (*const volatile mt32emu_refer_version_tag_ref)(void) = mt32emu_refer_version_tag;

#endif

#undef MT32EMU_REALLY_BUILD_VERSION_TAG
#undef MT32EMU_BUILD_VERSION_TAG
#undef MT32EMU_VERSION_TAG

#endif
