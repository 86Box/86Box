// Part of SAASound copyright 1998-2018 Dave Hooper <dave@beermex.com>
//
// SAAAmp.h: interface for the CSAAAmp class.
// This class handles Tone/Noise mixing, Envelope application and
// amplification.
//
//////////////////////////////////////////////////////////////////////

#ifndef SAAAMP_H_INCLUDED
#define SAAAMP_H_INCLUDED

class CSAAAmp  
{
private:
	int leftlevel;
	int leftlevela0x0e;
	int rightlevel;
	int rightlevela0x0e;
	int m_nOutputIntermediate;
	unsigned int m_nMixMode;
	CSAAFreq * const m_pcConnectedToneGenerator; // not const because amp calls ->Tick()
	const CSAANoise * const m_pcConnectedNoiseGenerator;
	const CSAAEnv * const m_pcConnectedEnvGenerator;
	const bool m_bUseEnvelope;
	mutable bool m_bMute;
	mutable bool m_bSync;
	mutable BYTE last_level_byte;
	int EffectiveAmplitude(int amp, int env) const;

public:
	CSAAAmp(CSAAFreq * const ToneGenerator, const CSAANoise * const NoiseGenerator, const CSAAEnv * const EnvGenerator);
	~CSAAAmp();

	void SetAmpLevel(BYTE level_byte); // really just a BYTE
	void SetToneMixer(BYTE bEnabled);
	void SetNoiseMixer(BYTE bEnabled);
	void Mute(bool bMute);
	void Sync(bool bSync);
	void Tick(void);
	void TickAndOutputStereo(unsigned int & left, unsigned int & right);

};

#endif	// SAAAMP_H_INCLUDED
