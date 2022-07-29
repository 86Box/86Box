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

#ifndef MT32EMU_TABLES_H
#define MT32EMU_TABLES_H

#include "globals.h"
#include "Types.h"

namespace MT32Emu {

class Tables {
private:
	Tables();
	Tables(Tables &);
	~Tables() {}

public:
	static const Tables &getInstance();

	// Constant LUTs

	// CONFIRMED: This is used to convert several parameters to amp-modifying values in the TVA envelope:
	// - PatchTemp.outputLevel
	// - RhythmTemp.outlevel
	// - PartialParam.tva.level
	// - expression
	// It's used to determine how much to subtract from the amp envelope's target value
	Bit8u levelToAmpSubtraction[101];

	// CONFIRMED: ...
	Bit8u envLogarithmicTime[256];

	// CONFIRMED: ...
	Bit8u masterVolToAmpSubtraction[101];

	// CONFIRMED:
	Bit8u pulseWidth100To255[101];

	Bit16u exp9[512];
	Bit16u logsin9[512];

	const Bit8u *resAmpDecayFactor;
}; // class Tables

} // namespace MT32Emu

#endif // #ifndef MT32EMU_TABLES_H
