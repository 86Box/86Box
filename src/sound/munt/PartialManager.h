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

#ifndef MT32EMU_PARTIALMANAGER_H
#define MT32EMU_PARTIALMANAGER_H

#include "globals.h"
#include "internals.h"
#include "Types.h"

namespace MT32Emu {

class Part;
class Partial;
class Poly;
class Synth;

class PartialManager {
private:
	Synth *synth;
	Part **parts;
	Poly **freePolys;
	Partial **partialTable;
	Bit8u numReservedPartialsForPart[9];
	Bit32u firstFreePolyIndex;
	int *inactivePartials; // Holds indices of inactive Partials in the Partial table
	Bit32u inactivePartialCount;

	bool abortFirstReleasingPolyWhereReserveExceeded(int minPart);
	bool abortFirstPolyPreferHeldWhereReserveExceeded(int minPart);

public:
	PartialManager(Synth *synth, Part **parts);
	~PartialManager();
	Partial *allocPartial(int partNum);
	unsigned int getFreePartialCount();
	void getPerPartPartialUsage(unsigned int perPartPartialUsage[9]);
	bool freePartials(unsigned int needed, int partNum);
	unsigned int setReserve(Bit8u *rset);
	void deactivateAll();
	bool produceOutput(int i, IntSample *leftBuf, IntSample *rightBuf, Bit32u bufferLength);
	bool produceOutput(int i, FloatSample *leftBuf, FloatSample *rightBuf, Bit32u bufferLength);
	bool shouldReverb(int i);
	void clearAlreadyOutputed();
	const Partial *getPartial(unsigned int partialNum) const;
	Poly *assignPolyToPart(Part *part);
	void polyFreed(Poly *poly);
	void partialDeactivated(int partialIndex);
}; // class PartialManager

} // namespace MT32Emu

#endif // #ifndef MT32EMU_PARTIALMANAGER_H
