// Part of SAASound copyright 1998-2018 Dave Hooper <dave@beermex.com>
//
// SAAEnv.h: interface for the CSAAEnv class.
//
//////////////////////////////////////////////////////////////////////

#ifndef SAAENV_H_INCLUDED
#define SAAENV_H_INCLUDED

class CSAAEnv  
{
private:
	int m_nLeftLevel, m_nRightLevel;
	ENVDATA const * m_pEnvData;

	bool m_bEnabled;
	bool m_bInvertRightChannel;
	BYTE m_nPhase;
	BYTE m_nPhasePosition;
	bool m_bEnvelopeEnded;
	char m_nPhaseAdd[2];
	char m_nCurrentPhaseAdd;
	bool m_bLooping;
	char m_nNumberOfPhases;
	char m_nResolution;
	char m_nInitialLevel;
	bool m_bNewData;
	BYTE m_nNextData;
	bool m_bClockExternally;
	static const ENVDATA cs_EnvData[8];

	void Tick(void);
	void SetLevels(void);
	void SetNewEnvData(int nData);

public:
	CSAAEnv();
	~CSAAEnv();

	void InternalClock(void);
	void ExternalClock(void);
	void SetEnvControl(int nData); // really just a BYTE
	int LeftLevel(void) const;
	int RightLevel(void) const;
	bool IsActive(void) const;

};

inline bool CSAAEnv::IsActive(void) const
{
	return m_bEnabled;
}

#endif	// SAAENV_H_INCLUDED
