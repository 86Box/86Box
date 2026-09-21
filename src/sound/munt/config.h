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

#ifndef MT32EMU_CONFIG_H
#define MT32EMU_CONFIG_H

#define MT32EMU_VERSION      "2.7.0"
#define MT32EMU_VERSION_MAJOR 2
#define MT32EMU_VERSION_MINOR 7
#define MT32EMU_VERSION_PATCH 0

/* Library Exports Configuration
 *
 * This reflects the API types actually provided by the library build.
 * 0: The full-featured C++ API is only available in this build. The client application may ONLY use MT32EMU_API_TYPE 0.
 * 1: The C-compatible API is only available. The library is built as a shared object, only C functions are exported,
 *    and thus the client application may NOT use MT32EMU_API_TYPE 0.
 * 2: The C-compatible API is only available. The library is built as a shared object, only the factory function
 *    is exported, and thus the client application may ONLY use MT32EMU_API_TYPE 2.
 * 3: All the available API types are provided by the library build.
 */
#define MT32EMU_EXPORTS_TYPE  3

#define MT32EMU_API_TYPE      0

#define MT32EMU_WITH_LIBSOXR_RESAMPLER 0
#define MT32EMU_WITH_LIBSAMPLERATE_RESAMPLER 0
#define MT32EMU_WITH_INTERNAL_RESAMPLER 1


#endif
