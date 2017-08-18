/* Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Dean Beeler, Jerome Fisher
 * Copyright (C) 2011-2017 Dean Beeler, Jerome Fisher, Sergey V. Mikayev
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

#ifndef MT32EMU_B_REVERB_MODEL_H
#define MT32EMU_B_REVERB_MODEL_H

#include "globals.h"
#include "internals.h"
#include "Enumerations.h"
#include "Types.h"

namespace MT32Emu {

class BReverbModel {
public:
	static BReverbModel *createBReverbModel(const ReverbMode mode, const bool mt32CompatibleModel, const RendererType rendererType);

	virtual ~BReverbModel() {}
	virtual bool isOpen() const = 0;
	// After construction or a close(), open() must be called at least once before any other call (with the exception of close()).
	virtual void open() = 0;
	// May be called multiple times without an open() in between.
	virtual void close() = 0;
	virtual void mute() = 0;
	virtual void setParameters(Bit8u time, Bit8u level) = 0;
	virtual bool isActive() const = 0;
	virtual bool isMT32Compatible(const ReverbMode mode) const = 0;
	virtual bool process(const IntSample *inLeft, const IntSample *inRight, IntSample *outLeft, IntSample *outRight, Bit32u numSamples) = 0;
	virtual bool process(const FloatSample *inLeft, const FloatSample *inRight, FloatSample *outLeft, FloatSample *outRight, Bit32u numSamples) = 0;
};

} // namespace MT32Emu

#endif // #ifndef MT32EMU_B_REVERB_MODEL_H
