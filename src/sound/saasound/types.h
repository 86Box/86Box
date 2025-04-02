// Part of SAASound copyright 1998-2018 Dave Hooper <dave@beermex.com>
//
// handy typedefs
//
//////////////////////////////////////////////////////////////////////

#ifndef TYPES_H_INCLUDED
#define TYPES_H_INCLUDED

#if  defined(__i386__) || defined(WIN32) || \
    (defined(__alpha__) || defined(__alpha)) || \
     defined(__arm__) || \
    (defined(__mips__) && defined(__MIPSEL__))
#else
#define __BIG_ENDIAN
#endif


#ifndef NULL
#define NULL	0
#endif

typedef struct
{
	int nNumberOfPhases;
	bool bLooping;
	int nLevels[2][2][16]; // [Resolution][Phase][Withinphase]
} ENVDATA;

#ifdef WIN32
extern "C" void _stdcall OutputDebugStringA (char*);
#endif

#endif
