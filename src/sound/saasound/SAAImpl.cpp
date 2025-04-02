// Part of SAASound copyright 1998-2018 Dave Hooper <dave@beermex.com>
//
// SAAImpl.cpp: implementation of the CSAASound class.
// the bones of the 'virtual SAA-1099' emulation
//
// the actual sound generation is carried out in the other classes;
// this class provides the output stage and the external interface only
//
//////////////////////////////////////////////////////////////////////

#include "SAASound.h"

#include "types.h"
#include "SAAImpl.h"
#include "defns.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CSAASoundInternal::CSAASoundInternal()
	:
m_chip(),
m_uParam(0),
m_uParamRate(0),
m_nClockRate(EXTERNAL_CLK_HZ),
m_nSampleRate(SAMPLE_RATE_HZ),
m_nOversample(DEFAULT_OVERSAMPLE),
#if defined(DEBUGSAA) || defined(USE_CONFIG_FILE)
m_bHighpass(false),
m_nDebugSample(0)
#else
m_bHighpass(false)
#endif
{
#ifdef USE_CONFIG_FILE
	m_Config.ReadConfig();
#endif

#if defined(DEBUGSAA)
	m_dbgfile.open(_T(DEBUG_SAA_REGISTER_LOG), std::ios_base::out);
	m_pcmfile.open(_T(DEBUG_SAA_PCM_LOG), std::ios_base::out | std::ios_base::binary);
#elif defined(USE_CONFIG_FILE)
	if (m_Config.m_bGenerateRegisterLogs)
		m_dbgfile.open(m_Config.m_strRegisterLogPath, std::ios_base::out);
	if (m_Config.m_bGeneratePcmLogs)
		m_pcmfile.open(m_Config.m_strPcmOutputPath, std::ios_base::out | std::ios_base::binary);

	if (m_Config.m_bGeneratePcmLogs && m_Config.m_bGeneratePcmSeparateChannels)
	{
		for (int i = 0; i < 6; i++)
		{
			m_channel_pcmfile[i].open(m_Config.getChannelPcmOutputPath(i), std::ios_base::out | std::ios_base::binary);
		}
	}


#endif
	// set parameters
	// TODO support defaults and overrides from config file
	// m_chip.SetSoundParameters(SAAP_FILTER | SAAP_11025 | SAAP_8BIT | SAAP_MONO);
	// reset the virtual SAA
	// m_chip.Clear();

	m_chip._SetClockRate(m_nClockRate);
	m_chip._SetOversample(m_nOversample);
}

CSAASoundInternal::~CSAASoundInternal()
{
	//
}

//////////////////////////////////////////////////////////////////////
// CSAASound members
//////////////////////////////////////////////////////////////////////

void CSAASoundInternal::SetClockRate(unsigned int nClockRate)
{
	m_nClockRate = nClockRate;
	m_chip._SetClockRate(m_nClockRate);
}

void CSAASoundInternal::Clear(void)
{
	// reinitialises virtual SAA:
	// sets reg 28 to 0x02; - sync and disabled
	// sets regs 00-31 (except 28) to 0x00;
	// sets reg 28 to 0x00;
	// sets current reg to 0
	WriteAddressData(28,2);
	for (int i=31; i>=0; i--)
	{
		if (i!=28) WriteAddressData(i,0);
	}
	WriteAddressData(28,0);
	WriteAddress(0);
}

void CSAASoundInternal::WriteData(BYTE nData)
{
	// originated from an OUT 255,d call
	m_chip._WriteData(nData);
#if defined(DEBUGSAA) || defined(USE_CONFIG_FILE)
#ifdef USE_CONFIG_FILE
	if (m_Config.m_bGenerateRegisterLogs)
	{
#endif
		m_dbgfile << m_nDebugSample << " " << (int)m_chip._ReadAddress() << ":" << (int)nData << std::endl;
#ifdef USE_CONFIG_FILE
	}
#endif
#endif
}

void CSAASoundInternal::WriteAddress(BYTE nReg)
{
	// originated from an OUT 511,r call
	m_chip._WriteAddress(nReg);
#if defined(DEBUGSAA) || defined(USE_CONFIG_FILE)
#ifdef USE_CONFIG_FILE
	if (m_Config.m_bGenerateRegisterLogs)
	{
#endif
		m_dbgfile << m_nDebugSample << " " << (int)nReg << ":";
		if (nReg==24)
		{
			m_dbgfile << "<!ENVO!>";
		}
		else if (nReg==25)
		{
			m_dbgfile << "<!ENV1!>";
		}
		m_dbgfile << std::endl;
#ifdef USE_CONFIG_FILE
	}
#endif
#endif
}

void CSAASoundInternal::WriteAddressData(BYTE nReg, BYTE nData)
{
	// performs WriteAddress(nReg) followed by WriteData(nData)
	m_chip._WriteAddress(nReg);
	m_chip._WriteData(nData);
}

#if 1
BYTE CSAASoundInternal::ReadAddress(void)
{
	// Not a real hardware function of the SAA-1099, which is write-only
	return(m_chip._ReadAddress());
}
#else
BYTE CSAASoundInternal::ReadAddress(void)
{
	// Not a real hardware function of the SAA-1099, which is write-only
	return(0);
}
#endif

void CSAASoundInternal::SetSoundParameters(SAAPARAM uParam)
{
	// set samplerate properties from uParam (deprecated but still supported)
	unsigned int nSampleRate = m_nSampleRate;
	switch (uParam & SAAP_MASK_SAMPLERATE)
	{
	case SAAP_44100:
		nSampleRate = 44100;
		m_uParamRate = (m_uParamRate & ~SAAP_MASK_SAMPLERATE) | SAAP_44100;
		break;
	case SAAP_22050:
		nSampleRate = 22050;
		m_uParamRate = (m_uParamRate & ~SAAP_MASK_SAMPLERATE) | SAAP_22050;
		break;
	case SAAP_11025:
		nSampleRate = 11025;
		m_uParamRate = (m_uParamRate & ~SAAP_MASK_SAMPLERATE) | SAAP_11025;
		break;
	case 0:// change nothing!
	default:
		break;
	}
	
	if (nSampleRate != m_nSampleRate)
	{
		m_nSampleRate = nSampleRate;
		m_chip._SetSampleRate(m_nSampleRate);
	}

	// set filter properties from uParam
	m_uParam = (m_uParam & ~SAAP_MASK_FILTER) | (uParam & SAAP_MASK_FILTER);
	 
	m_bHighpass=true;
}

void CSAASoundInternal::SetSampleRate(unsigned int nSampleRate)
{
	if (nSampleRate != m_nSampleRate)
	{
		m_nSampleRate = nSampleRate;
		m_chip._SetSampleRate(m_nSampleRate);
	}
}

void CSAASoundInternal::SetOversample(unsigned int nOversample)
{
	if (nOversample != m_nOversample)
	{
		m_nOversample = nOversample;
		m_chip._SetOversample(m_nOversample);
	}
}

SAAPARAM CSAASoundInternal::GetCurrentSoundParameters(void)
{
	return m_uParam | m_uParamRate;
}

unsigned short CSAASoundInternal::GetCurrentBytesPerSample(void)
{
	// 16 bit stereo => 4 bytes per sample
	return 4;
}

/*static*/ unsigned short CSAASound::GetBytesPerSample(SAAPARAM uParam)
{
	// 16 bit stereo => 4 bytes per sample
	switch (uParam & (SAAP_MASK_CHANNELS | SAAP_MASK_BITDEPTH))
	{
		case SAAP_STEREO | SAAP_16BIT:
			return 4;
		default:
			return 0;
	}
}

unsigned long CSAASoundInternal::GetCurrentSampleRate(void)
{
	return CSAASound::GetSampleRate(m_uParamRate);
}

/*static*/ unsigned long CSAASound::GetSampleRate(SAAPARAM uParam) // static member function
{
	switch (uParam & SAAP_MASK_SAMPLERATE)
	{
		case SAAP_11025:
			return 11025;
		case SAAP_22050:
			return 22050;
		case SAAP_44100:
			return 44100;
		default:
			return 0;
	}
}

#if defined(USE_CONFIG_FILE) || (defined(DEFAULT_BOOST) && DEFAULT_BOOST>1)
#define DO_BOOST
#endif

void scale_for_output(unsigned int left_input, unsigned int right_input,
	double oversample_scalar, bool highpass, double boost,
	double& filterout_z1_left, double& filterout_z1_right,
	BYTE* &pBuffer)
{
	double float_left = (double)left_input;
	double float_right = (double)right_input;
	float_left /= oversample_scalar;
	float_right /= oversample_scalar;

	// scale output into good range
	float_left *= DEFAULT_UNBOOSTED_MULTIPLIER;
	float_right *= DEFAULT_UNBOOSTED_MULTIPLIER;

	if (highpass)
	{
		/* cutoff = 5 Hz (say)
			const double b1 = exp(-2.0 * M_PI * (Fc/Fs))
			const double a0 = 1.0 - b1;
		*/
		const double b1 = 0.99928787;
		const double a0 = 1.0 - b1;

		filterout_z1_left = float_left * a0 + filterout_z1_left * b1;
		filterout_z1_right = float_right * a0 + filterout_z1_right * b1;
		float_left -= filterout_z1_left;
		float_right -= filterout_z1_right;
	}

	// multiply by boost, if defined
#if defined(DO_BOOST)
	float_left *= boost;
	float_right *= boost;
#endif
	// convert to 16-bit signed range with hard clipping
	signed short left_output = (signed short)(float_left > 32767 ? 32767 : float_left < -32768 ? -32768 : float_left);
	signed short right_output = (signed short)(float_right > 32767 ? 32767 : float_right < -32768 ? -32768 : float_right);

	*pBuffer++ = left_output & 0x00ff;
	*pBuffer++ = (left_output >> 8) & 0x00ff;
	*pBuffer++ = right_output & 0x00ff;
	*pBuffer++ = (right_output >> 8) & 0x00ff;
}

void CSAASoundInternal::GenerateMany(BYTE* pBuffer, unsigned long nSamples)
{
	unsigned int left_mixed, right_mixed;
	static double filterout_z1_left_mixed = 0, filterout_z1_right_mixed = 0;

#if defined(DEBUGSAA) || defined(USE_CONFIG_FILE)
	BYTE* pBufferStart = pBuffer;
	unsigned long nTotalSamples = nSamples;
#endif

#if defined(DO_BOOST)
#if defined(USE_CONFIG_FILE)
	double nBoost = m_Config.m_nBoost;
#else
	double nBoost = DEFAULT_BOOST;
#endif
#else
	double nBoost = 1.0;
#endif

	double oversample = double(1 << m_nOversample);

#if defined(USE_CONFIG_FILE)
	static double filterout_z1_left_0 = 0, filterout_z1_right_0 = 0;
	static double filterout_z1_left_1 = 0, filterout_z1_right_1 = 0;
	static double filterout_z1_left_2 = 0, filterout_z1_right_2 = 0;
	static double filterout_z1_left_3 = 0, filterout_z1_right_3 = 0;
	static double filterout_z1_left_4 = 0, filterout_z1_right_4 = 0;
	static double filterout_z1_left_5 = 0, filterout_z1_right_5 = 0;

	if (m_Config.m_bGeneratePcmLogs && m_Config.m_bGeneratePcmSeparateChannels)
	{
		unsigned int left0, right0, left1, right1, left2, right2, left3, right3, left4, right4, left5, right5;
		BYTE* pChannelBufferPtr[6] = { m_pChannelBuffer[0], m_pChannelBuffer[1], m_pChannelBuffer[2], m_pChannelBuffer[3], m_pChannelBuffer[4], m_pChannelBuffer[5] };

		while (nSamples--)
		{
			m_chip._TickAndOutputSeparate(left_mixed, right_mixed,
				left0, right0,
				left1, right1,
				left2, right2,
				left3, right3,
				left4, right4,
				left5, right5);
			scale_for_output(left_mixed, right_mixed, oversample, m_bHighpass, nBoost, filterout_z1_left_mixed, filterout_z1_right_mixed, pBuffer);

			// and the separate channels
			scale_for_output(left0, right0, oversample, m_bHighpass, nBoost, filterout_z1_left_0, filterout_z1_right_0, pChannelBufferPtr[0]);
			scale_for_output(left1, right1, oversample, m_bHighpass, nBoost, filterout_z1_left_1, filterout_z1_right_1, pChannelBufferPtr[1]);
			scale_for_output(left2, right2, oversample, m_bHighpass, nBoost, filterout_z1_left_2, filterout_z1_right_2, pChannelBufferPtr[2]);
			scale_for_output(left3, right3, oversample, m_bHighpass, nBoost, filterout_z1_left_3, filterout_z1_right_3, pChannelBufferPtr[3]);
			scale_for_output(left4, right4, oversample, m_bHighpass, nBoost, filterout_z1_left_4, filterout_z1_right_4, pChannelBufferPtr[4]);
			scale_for_output(left5, right5, oversample, m_bHighpass, nBoost, filterout_z1_left_5, filterout_z1_right_5, pChannelBufferPtr[5]);

			// flush channel output PCM buffers when full
			if (pChannelBufferPtr[0] >= m_pChannelBuffer[0] + CHANNEL_BUFFER_SIZE)
			{
				for (int i = 0; i < 6; i++)
				{
					m_channel_pcmfile[i].write((const char*)m_pChannelBuffer[i], CHANNEL_BUFFER_SIZE);
					pChannelBufferPtr[i] = m_pChannelBuffer[i];
				}
			}
		}
		// flush remaining channel PCM output data
		if (pChannelBufferPtr[0] >= m_pChannelBuffer[0])
		{
			for (int i = 0; i < 6; i++)
			{
				m_channel_pcmfile[i].write((const char*)m_pChannelBuffer[i], pChannelBufferPtr[i]-m_pChannelBuffer[i]);
			}
		}
	}
	else
	{
#endif
		while (nSamples--)
		{
			m_chip._TickAndOutputStereo(left_mixed, right_mixed);
			scale_for_output(left_mixed, right_mixed, oversample, m_bHighpass, nBoost, filterout_z1_left_mixed, filterout_z1_right_mixed, pBuffer);
		}

#if defined(USE_CONFIG_FILE)
	}
#endif

#if defined(DEBUGSAA) || defined(USE_CONFIG_FILE)
#ifdef USE_CONFIG_FILE
	if (m_Config.m_bGeneratePcmLogs)
	{
#endif
		m_pcmfile.write((const char *)pBufferStart, nTotalSamples * (unsigned long)GetCurrentBytesPerSample());
		m_nDebugSample += nTotalSamples;
#ifdef USE_CONFIG_FILE
	}
#endif

#endif
}

///////////////////////////////////////////////////////

LPCSAASOUND SAAAPI CreateCSAASound(void)
{
	return (new CSAASoundInternal);
}

void SAAAPI DestroyCSAASound(LPCSAASOUND object)
{
	delete (object);
}


/* thoughts on lowpass filtering as part of oversampling.
I tried this and really it didn't seem to make a lot of (audible) difference.

// lowpass oversample filter adds complexity and not particularly audibly better than simple averaging.
// use_lowpass_oversample_filter_average_output adds an additional averaging step to the output of the oversample
// filter.  this seems critical, because without this, the raw output of the lowpass filter is full of aliases
// If use_lowpass_oversample_filter is False, then the _average_output flag is ignored.
// Default, use_lowpass_oversample_filter is False, it sounds just fine really.

//#define USE_LOWPASS_OVERSAMPLE_FILTER
#undef USE_LOWPASS_OVERSAMPLE_FILTER
//#define USE_LOWPASS_OVERSAMPLE_FILTER_AVERAGE_OUTPUT
#undef USE_LOWPASS_OVERSAMPLE_FILTER_AVERAGE_OUTPUT

#ifdef USE_LOWPASS_OVERSAMPLE_FILTER
static double oversample_lp_filterout_z1_left_stages[10] = { 0,0,0,0,0,0,0,0,0,0 };
static double oversample_lp_filterout_z1_right_stages[10] = { 0,0,0,0,0,0,0,0,0,0 };
double averaged_filterout_left = 0.0, averaged_filterout_right = 0.0;
const int nStages = 10;
for (int i = 0; i < 1 << m_nOversample; i++)
{
	Noise[0]->Tick();
	Noise[1]->Tick();
	f_left = f_right = 0;
	for (int c = 0; c < 6; c++)
	{
		Amp[c]->TickAndOutputStereo(temp_left, temp_right);
		f_left += (double)temp_left;
		f_right += (double)temp_right;
	}
	// apply lowpass here.
	// HACK: ASSUME m_nOversample is 64 (I was experimenting only using the 64x oversample anyway)
	// therefore Fs = 44100*64
	// let's set Fc = 10kHz
	// so Fc/Fs = 0.00354308390022675736961451247166
	// const double b1 = exp(-2.0 * M_PI * (Fc/Fs))
	// const double a0 = 1.0 - b1;
	// const double b1 = 0.9779841137335348363722276130195;
	const double b1 = 0.977;
	const double a0 = 1.0 - b1;

	oversample_lp_filterout_z1_left_stages[0] = f_left * a0 + oversample_lp_filterout_z1_left_stages[0] * b1;
	for (int stage = 1; stage < nStages; stage++)
		oversample_lp_filterout_z1_left_stages[stage] = oversample_lp_filterout_z1_left_stages[stage - 1] * a0 + oversample_lp_filterout_z1_left_stages[stage] * b1;
	oversample_lp_filterout_z1_right_stages[0] = f_right * a0 + oversample_lp_filterout_z1_right_stages[0] * b1;
	for (int stage = 1; stage < nStages; stage++)
		oversample_lp_filterout_z1_right_stages[stage] = oversample_lp_filterout_z1_right_stages[stage - 1] * a0 + oversample_lp_filterout_z1_right_stages[stage] * b1;

#ifdef USE_LOWPASS_OVERSAMPLE_FILTER_AVERAGE_OUTPUT
	averaged_filterout_left += oversample_lp_filterout_4z1_left;
	averaged_filterout_right += oversample_lp_filterout_4z1_right;
#endif
}

// by the end of this loop we will have computed the oversample lowpass filter m_nOversample times
// and yielded exactly ONE sample output.
#ifdef USE_LOWPASS_OVERSAMPLE_FILTER_AVERAGE_OUTPUT
f_left = averaged_filterout_left / (1 << m_nOversample);
f_right = averaged_filterout_right / (1 << m_nOversample);
#else
f_left = oversample_lp_filterout_z1_left_stages[nStages - 1];
f_right = oversample_lp_filterout_z1_right_stages[nStages - 1];
#endif

#else
	// do the simple 1/N averaging which is easier and sounds good enough

#endif

*/

