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

#include "globals.h"

extern "C" {
// Here's a list of all tagged minor library versions through global (potentially versioned) symbols.
// An application that's been linked with an older library version will be able to find a matching tag,
// while for an application linked with a newer library version there will be no match.

MT32EMU_EXPORT_V(2.5) extern const volatile char mt32emu_2_5 = 0;
MT32EMU_EXPORT_V(2.6) extern const volatile char mt32emu_2_6 = 0;
MT32EMU_EXPORT_V(2.7) extern const volatile char mt32emu_2_7 = 0;

#if MT32EMU_VERSION_MAJOR > 2 || MT32EMU_VERSION_MINOR > 7
#error "Missing version tag definition for current library version"
#endif
}
