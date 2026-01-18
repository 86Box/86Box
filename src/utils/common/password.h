/*
 Legal Notice: Some portions of the source code contained in this file were
 derived from the source code of Encryption for the Masses 2.02a, which is
 Copyright (c) 1998-2000 Paul Le Roux and which is governed by the 'License
 Agreement for Encryption for the Masses'. Modifications and additions to
 the original source code (contained in this file) and all other portions of
 this file are Copyright (c) 2003-2008 TrueCrypt Foundation and are governed
 by the TrueCrypt License 2.4 the full text of which is contained in the
 file License.txt included in TrueCrypt binary and source code distribution
 packages. */

#ifndef PASSWORD_H
#define PASSWORD_H

#include <inttypes.h>

// User text input limits
#define MIN_PASSWORD			1		// Minimum password length
#define MAX_PASSWORD			64		// Maximum password length

#define PASSWORD_LEN_WARNING	20		// Display a warning when a password is shorter than this

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	// Modifying this structure can introduce incompatibility with previous versions
	int32_t Length;
	unsigned char Text[MAX_PASSWORD + 1];
	char Pad[3]; // keep 64-bit alignment
} Password;

#ifdef __cplusplus
}
#endif

#endif	// PASSWORD_H
