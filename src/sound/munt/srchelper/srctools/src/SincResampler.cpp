/* Copyright (C) 2015-2020 Sergey V. Mikayev
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

#include <cmath>

#ifdef SRCTOOLS_SINC_RESAMPLER_DEBUG_LOG
#include <iostream>
#endif

#include "../include/SincResampler.h"

#ifndef M_PI
static const double M_PI = 3.1415926535897932;
#endif

using namespace SRCTools;

using namespace SincResampler;

using namespace Utils;

void Utils::computeResampleFactors(unsigned int &upsampleFactor, double &downsampleFactor, const double inputFrequency, const double outputFrequency, const unsigned int maxUpsampleFactor) {
	static const double RATIONAL_RATIO_ACCURACY_FACTOR = 1E15;

	upsampleFactor = static_cast<unsigned int>(outputFrequency);
	unsigned int downsampleFactorInt = static_cast<unsigned int>(inputFrequency);
	if ((upsampleFactor == outputFrequency) && (downsampleFactorInt == inputFrequency)) {
		// Input and output frequencies are integers, try to reduce them
		const unsigned int gcd = greatestCommonDivisor(upsampleFactor, downsampleFactorInt);
		if (gcd > 1) {
			upsampleFactor /= gcd;
			downsampleFactor = downsampleFactorInt / gcd;
		} else {
			downsampleFactor = downsampleFactorInt;
		}
		if (upsampleFactor <= maxUpsampleFactor) return;
	} else {
		// Try to recover rational resample ratio by brute force
		double inputToOutputRatio = inputFrequency / outputFrequency;
		for (unsigned int i = 1; i <= maxUpsampleFactor; ++i) {
			double testFactor = inputToOutputRatio * i;
			if (floor(RATIONAL_RATIO_ACCURACY_FACTOR * testFactor + 0.5) == RATIONAL_RATIO_ACCURACY_FACTOR * floor(testFactor + 0.5)) {
				// inputToOutputRatio found to be rational within the accuracy
				upsampleFactor = i;
				downsampleFactor = floor(testFactor + 0.5);
				return;
			}
		}
	}
	// Use interpolation of FIR taps as the last resort
	upsampleFactor = maxUpsampleFactor;
	downsampleFactor = maxUpsampleFactor * inputFrequency / outputFrequency;
}

unsigned int Utils::greatestCommonDivisor(unsigned int a, unsigned int b) {
	while (0 < b) {
		unsigned int r = a % b;
		a = b;
		b = r;
	}
	return a;
}

double KaizerWindow::estimateBeta(double dbRipple) {
	return 0.1102 * (dbRipple - 8.7);
}

unsigned int KaizerWindow::estimateOrder(double dbRipple, double fp, double fs) {
	const double transBW = (fs - fp);
	return static_cast<unsigned int>(ceil((dbRipple - 8) / (2.285 * 2 * M_PI * transBW)));
}

double KaizerWindow::bessel(const double x) {
	static const double EPS = 1.11E-16;

	double sum = 0.0;
	double f = 1.0;
	for (unsigned int i = 1;; ++i) {
		f *= (0.5 * x / i);
		double f2 = f * f;
		if (f2 <= sum * EPS) break;
		sum += f2;
	}
	return 1.0 + sum;
}

void KaizerWindow::windowedSinc(FIRCoefficient kernel[], const unsigned int order, const double fc, const double beta, const double amp) {
	const double fc_pi = M_PI * fc;
	const double recipOrder = 1.0 / order;
	const double mult = 2.0 * fc * amp / bessel(beta);
	for (int i = order, j = 0; 0 <= i; i -= 2, ++j) {
		double xw = i * recipOrder;
		double win = bessel(beta * sqrt(fabs(1.0 - xw * xw)));
		double xs = i * fc_pi;
		double sinc = (i == 0) ? 1.0 : sin(xs) / xs;
		FIRCoefficient imp = FIRCoefficient(mult * sinc * win);
		kernel[j] = imp;
		kernel[order - j] = imp;
	}
}

ResamplerStage *SincResampler::createSincResampler(const double inputFrequency, const double outputFrequency, const double passbandFrequency, const double stopbandFrequency, const double dbSNR, const unsigned int maxUpsampleFactor) {
	unsigned int upsampleFactor;
	double downsampleFactor;
	computeResampleFactors(upsampleFactor, downsampleFactor, inputFrequency, outputFrequency, maxUpsampleFactor);
	double baseSamplePeriod = 1.0 / (inputFrequency * upsampleFactor);
	double fp = passbandFrequency * baseSamplePeriod;
	double fs = stopbandFrequency * baseSamplePeriod;
	double fc = 0.5 * (fp + fs);
	double beta = KaizerWindow::estimateBeta(dbSNR);
	unsigned int order = KaizerWindow::estimateOrder(dbSNR, fp, fs);
	const unsigned int kernelLength = order + 1;

#ifdef SRCTOOLS_SINC_RESAMPLER_DEBUG_LOG
	std::clog << "FIR: " << upsampleFactor << "/" << downsampleFactor << ", N=" << kernelLength << ", NPh=" << kernelLength / double(upsampleFactor) << ", C=" << 0.5 / fc << ", fp=" << fp << ", fs=" << fs << ", M=" << maxUpsampleFactor << std::endl;
#endif

	FIRCoefficient *windowedSincKernel = new FIRCoefficient[kernelLength];
	KaizerWindow::windowedSinc(windowedSincKernel, order, fc, beta, upsampleFactor);
	ResamplerStage *windowedSincStage = new FIRResampler(upsampleFactor, downsampleFactor, windowedSincKernel, kernelLength);
	delete[] windowedSincKernel;
	return windowedSincStage;
}
