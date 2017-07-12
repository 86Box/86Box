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

#ifndef SRCTOOLS_SINC_RESAMPLER_H
#define SRCTOOLS_SINC_RESAMPLER_H

#include "FIRResampler.h"

namespace SRCTools {

class ResamplerStage;

namespace SincResampler {

	ResamplerStage *createSincResampler(const double inputFrequency, const double outputFrequency, const double passbandFrequency, const double stopbandFrequency, const double dbSNR, const unsigned int maxUpsampleFactor);

	namespace Utils {
		void computeResampleFactors(unsigned int &upsampleFactor, double &downsampleFactor, const double inputFrequency, const double outputFrequency, const unsigned int maxUpsampleFactor);
		unsigned int greatestCommonDivisor(unsigned int a, unsigned int b);
	}

	namespace KaizerWindow {
		double estimateBeta(double dbRipple);
		unsigned int estimateOrder(double dbRipple, double fp, double fs);
		double bessel(const double x);
		void windowedSinc(FIRCoefficient kernel[], const unsigned int order, const double fc, const double beta, const double amp);
	}

} // namespace SincResampler

} // namespace SRCTools

#endif // SRCTOOLS_SINC_RESAMPLER_H
