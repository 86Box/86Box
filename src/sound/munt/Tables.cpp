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

#include "internals.h"

#include "Tables.h"
#include "mmath.h"

namespace MT32Emu {

// UNUSED: const int MIDDLEC = 60;

const Tables &Tables::getInstance() {
	static const Tables instance;
	return instance;
}

Tables::Tables() {
	for (int lf = 0; lf <= 100; lf++) {
		// CONFIRMED:KG: This matches a ROM table found by Mok
		float fVal = (2.0f - LOG10F(float(lf) + 1.0f)) * 128.0f;
		int val = int(fVal + 1.0);
		if (val > 255) {
			val = 255;
		}
		levelToAmpSubtraction[lf] = Bit8u(val);
	}

	envLogarithmicTime[0] = 64;
	for (int lf = 1; lf <= 255; lf++) {
		// CONFIRMED:KG: This matches a ROM table found by Mok
		envLogarithmicTime[lf] = Bit8u(ceil(64.0f + LOG2F(float(lf)) * 8.0f));
	}

#if 0
	// The table below is to be used in conjunction with emulation of VCA of newer generation units which is currently missing.
	// These relatively small values are rather intended to fine-tune the overall amplification of the VCA.
	// CONFIRMED: Based on a table found by Mok in the LAPC-I control ROM
	// Note that this matches the MT-32 table, but with the values clamped to a maximum of 8.
	memset(masterVolToAmpSubtraction, 8, 71);
	memset(masterVolToAmpSubtraction + 71, 7, 3);
	memset(masterVolToAmpSubtraction + 74, 6, 4);
	memset(masterVolToAmpSubtraction + 78, 5, 3);
	memset(masterVolToAmpSubtraction + 81, 4, 4);
	memset(masterVolToAmpSubtraction + 85, 3, 3);
	memset(masterVolToAmpSubtraction + 88, 2, 4);
	memset(masterVolToAmpSubtraction + 92, 1, 4);
	memset(masterVolToAmpSubtraction + 96, 0, 5);
#else
	// CONFIRMED: Based on a table found by Mok in the MT-32 control ROM
	masterVolToAmpSubtraction[0] = 255;
	for (int masterVol = 1; masterVol <= 100; masterVol++) {
		masterVolToAmpSubtraction[masterVol] = Bit8u(106.31 - 16.0f * LOG2F(float(masterVol)));
	}
#endif

	for (int i = 0; i <= 100; i++) {
		pulseWidth100To255[i] = Bit8u(i * 255 / 100.0f + 0.5f);
		//synth->printDebug("%d: %d", i, pulseWidth100To255[i]);
	}

	// The LA32 chip contains an exponent table inside. The table contains 12-bit integer values.
	// The actual table size is 512 rows. The 9 higher bits of the fractional part of the argument are used as a lookup address.
	// To improve the precision of computations, the lower bits are supposed to be used for interpolation as the LA32 chip also
	// contains another 512-row table with inverted differences between the main table values.
	for (int i = 0; i < 512; i++) {
		exp9[i] = Bit16u(8191.5f - EXP2F(13.0f + ~i / 512.0f));
	}

	// There is a logarithmic sine table inside the LA32 chip. The table contains 13-bit integer values.
	for (int i = 1; i < 512; i++) {
		logsin9[i] = Bit16u(0.5f - LOG2F(sin((i + 0.5f) / 1024.0f * FLOAT_PI)) * 1024.0f);
	}

	// The very first value is clamped to the maximum possible 13-bit integer
	logsin9[0] = 8191;

	// found from sample analysis
	static const Bit8u resAmpDecayFactorTable[] = {31, 16, 12, 8, 5, 3, 2, 1};
	resAmpDecayFactor = resAmpDecayFactorTable;
}

} // namespace MT32Emu
