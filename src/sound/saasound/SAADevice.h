// Part of SAASound copyright 2020 Dave Hooper <dave@beermex.com>
//
// SAADevice.h: connecting the subcomponents of the SAA1099 together.
// This class handles device inputs and outputs (clocking, data and
// address bus, and simulated output)
//
//////////////////////////////////////////////////////////////////////

#ifndef SAADEVICE_H_INCLUDED
#define SAADEVICE_H_INCLUDED

#include "SAASound.h"
#include "SAANoise.h"
#include "SAAEnv.h"
#include "SAAFreq.h"
#include "SAAAmp.h"

class CSAADevice
{
private:
	int m_nCurrentSaaReg;
	bool m_bOutputEnabled;
	bool m_bSync;
	bool m_bHighpass;
	int m_nOversample;

	CSAANoise m_Noise0, m_Noise1;
	CSAAEnv m_Env0, m_Env1;
	CSAAFreq m_Osc0, m_Osc1, m_Osc2, m_Osc3, m_Osc4, m_Osc5;
	CSAAAmp m_Amp0, m_Amp1, m_Amp2, m_Amp3, m_Amp4, m_Amp5;

	CSAANoise* Noise[2];
	CSAAEnv* Env[2];
	CSAAFreq* Osc[6];
	CSAAAmp* Amp[6];

#if defined(DEBUG) || defined(DEBUGSAA)
	BYTE m_Reg[32];
#endif

public:
	CSAADevice();
	~CSAADevice();

	void _WriteAddress(BYTE nReg);
	void _WriteData(BYTE nData);
#if 1
	BYTE _ReadAddress(void);
#endif
#if defined(DEBUG)
	BYTE _ReadData(void);
#endif

	void _SetClockRate(unsigned int nClockRate);
	void _SetSampleRate(unsigned int nSampleRate);
	void _SetOversample(unsigned int nOversample);
	void _TickAndOutputStereo(unsigned int& left_mixed, unsigned int& right_mixed);
	void _TickAndOutputSeparate(unsigned int& left_mixed, unsigned int& right_mixed,
		unsigned int& left0, unsigned int& right0,
		unsigned int& left1, unsigned int& right1,
		unsigned int& left2, unsigned int& right2,
		unsigned int& left3, unsigned int& right3,
		unsigned int& left4, unsigned int& right4,
		unsigned int& left5, unsigned int& right5
	);

};

#endif //  SAADEVICE_H_INCLUDED