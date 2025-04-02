// Part of SAASound copyright 1998-2018 Dave Hooper <dave@beermex.com>
//
// SAANoise.cpp: implementation of the CSAANoise class.
// One noise generator
//
// After construction, it's important to SetSampleRate before
// trying to use the generator.
// (Just because the CSAANoise object has a default samplerate
//  doesn't mean you should rely on it)
//
//////////////////////////////////////////////////////////////////////

#include "SAASound.h"

#include "types.h"
#include "SAANoise.h"
#include "defns.h"


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CSAANoise::CSAANoise()
:
m_nCounter(0),
m_nCounter_low(0),
m_nOversample(0),
m_nCounterLimit_low(1),
m_bSync(false),
m_nSampleRate(SAMPLE_RATE_HZ),
m_nSourceMode(0),
m_nRand(1)
{
	_SetClockRate(EXTERNAL_CLK_HZ);
	m_nAdd = m_nAddBase;
}

CSAANoise::CSAANoise(unsigned long seed)
:
m_nCounter(0),
m_nCounter_low(0),
m_nOversample(0),
m_nCounterLimit_low(1),
m_bSync(false),
m_nSampleRate(SAMPLE_RATE_HZ),
m_nSourceMode(0),
m_nRand(seed)
{
	_SetClockRate(EXTERNAL_CLK_HZ);
	m_nAdd = m_nAddBase;
}

CSAANoise::~CSAANoise()
{
	// Nothing to do
}

void CSAANoise::_SetClockRate(int nClockRate)
{
	// at 8MHz the clock rate is 31.250kHZ
	// This is simply the clock rate divided by 256 i.e. 2^8
	// We then shift this by 2^12 (like the Freq) for better
	// period accuracy.  So that's the same as shifting by (12-8)
	m_nAddBase = nClockRate << (12 - 8);
}

void CSAANoise::Seed(unsigned long seed)
{
	m_nRand = seed;
}

void CSAANoise::SetSource(int nSource)
{
	m_nSourceMode = nSource;
	m_nAdd = m_nAddBase >> m_nSourceMode;
}

void CSAANoise::Trigger(void)
{
	// Trigger only does anything useful when we're
	// clocking from the frequency generator - i.e
	// if bUseFreqGen = true (i.e. SourceMode = 3)
	
	// So if we're clocking from the noise generator
	// clock (ie, SourceMode = 0, 1 or 2) then do nothing

//	No point actually checking m_bSync here ... because if sync is true,
//	then frequency generators won't actually be generating Trigger pulses
//	so we wouldn't even get here!
	// EXCEPT - cool edge case:  if sync is set, then actually the Noise Generator
	// is triggered on EVERY CLOCK PULSE (i.e. 8MHz noise).  So indeed it is correct
	// to not check for sync here.  NEEDS TEST CASE.

	if (m_nSourceMode == 3)
	{
		ChangeLevel();
	}
}

void CSAANoise::Tick(void)
{
	// Tick only does anything useful when we're
	// clocking from the noise generator clock
	// (ie, SourceMode = 0, 1 or 2)
	
	// So, if SourceMode = 3 (ie, we're clocking from a
	// frequency generator ==> bUseFreqGen = true)
	// then do nothing
	if ( (!m_bSync) && (m_nSourceMode!=3) )
	{
		m_nCounter += m_nAdd;
		while (m_nCounter >= (m_nSampleRate<<12))
		{
			m_nCounter -= (m_nSampleRate<<12);
			m_nCounter_low++;
			if (m_nCounter_low >= m_nCounterLimit_low)
			{
				m_nCounter_low = 0;
				ChangeLevel();
			}
		}
	}
}

void CSAANoise::Sync(bool bSync)
{
	if (bSync)
	{
		m_nCounter = 0;
		m_nCounter_low = 0;
	}
	m_bSync = bSync;
}


void CSAANoise::_SetSampleRate(int nSampleRate)
{
	m_nSampleRate = nSampleRate;
}


void CSAANoise::_SetOversample(unsigned int oversample)
{
	// oversample is a power of 2 i.e.
	// if oversample == 2 then 4x oversample
	// if oversample == 6 then 64x oversample
	if (oversample < m_nOversample)
	{
		m_nCounter_low <<= (m_nOversample - oversample);
	}
	else
	{
		m_nCounter_low >>= (oversample - m_nOversample);
	}

	m_nCounterLimit_low = 1<<oversample;
	m_nOversample = oversample;
}

inline void CSAANoise::ChangeLevel(void)
{
	/*
	https://www.vogons.org/viewtopic.php?f=9&t=51695
	SAA1099P noise generator as documented by Jepael
	18-bit Galois LFSR
	Feedback polynomial = x^18 + x^11 + x^1
	Period = 2^18-1 = 262143 bits
	Verified to match recorded noise from my SAA1099P
	*/

	if (m_nRand & 1)
	{
		m_nRand = (m_nRand >> 1) ^ 0x20400;
	}
	else
	{
		m_nRand >>= 1;
	}
}
