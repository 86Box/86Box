// Part of SAASound copyright 2020 Dave Hooper <dave@beermex.com>
//
// SAAConfig.h: configuration file handler class
//
//////////////////////////////////////////////////////////////////////

#include "defns.h"
#ifdef USE_CONFIG_FILE

#ifndef SAA_CONFIG_H_INCLUDED
#define SAA_CONFIG_H_INCLUDED

#define INI_READONLY
#define INI_ANSIONLY  /*nb not really 'ANSI', this just forces all read/write to use 8-bit char*/
#include "minIni/minIni.h"

class SAAConfig
{
private:
	minIni m_minIni;
	bool m_bHasReadConfig;

public:
	bool m_bGenerateRegisterLogs;
	bool m_bGeneratePcmLogs;
	bool m_bGeneratePcmSeparateChannels;
	t_string m_strRegisterLogPath;
	t_string m_strPcmOutputPath;
	unsigned int m_nOversample;
	bool m_bHighpass;
	double m_nBoost;

	SAAConfig();
	void ReadConfig();

	t_string getChannelPcmOutputPath(int);
};

#endif  // SAA_CONFIG_H_INCLUDED

#endif // USE_CONFIG_FILE