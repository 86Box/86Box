/* Copyright (C) 2015-2017 Sergey V. Mikayev
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

#ifndef SRCTOOLS_FLOAT_SAMPLE_PROVIDER_H
#define SRCTOOLS_FLOAT_SAMPLE_PROVIDER_H

namespace SRCTools {

typedef float FloatSample;

/** Interface defines an abstract source of samples. It can either define a single channel stream or a stream with interleaved channels. */
class FloatSampleProvider {
public:
	virtual ~FloatSampleProvider() {}

	virtual void getOutputSamples(FloatSample *outBuffer, unsigned int size) = 0;
};

} // namespace SRCTools

#endif // SRCTOOLS_FLOAT_SAMPLE_PROVIDER_H
