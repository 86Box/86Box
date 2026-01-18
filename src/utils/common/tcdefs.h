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

#ifndef TCDEFS_H
#define TCDEFS_H

#include <inttypes.h>
#include <stdlib.h>

#define TC_APP_NAME						"TrueCrypt"

// Version displayed to user
#define VERSION_STRING					"5.1a"

// Version number to compare against driver
#define VERSION_NUM						0x051a

// Version number written to volume header during format,
// specifies the minimum program version required to mount the volume
#define VOL_REQ_PROG_VERSION			0x0500

// Volume header version
#define VOLUME_HEADER_VERSION			0x0003

// Sector size of encrypted filesystem, which may differ from sector size
// of host filesystem/device (this is fully supported since v4.3).
#define SECTOR_SIZE                     512

#define BYTES_PER_KB                    1024LL
#define BYTES_PER_MB                    1048576LL
#define BYTES_PER_GB                    1073741824LL
#define BYTES_PER_TB                    1099511627776LL
#define BYTES_PER_PB                    1125899906842624LL

/* GUI/driver errors */

#define MAX_128BIT_BLOCK_VOLUME_SIZE	BYTES_PER_PB			// Security bound (128-bit block XTS mode)
#define MAX_VOLUME_SIZE_GENERAL			0x7fffFFFFffffFFFFLL	// Signed 64-bit integer file offset values
#define MAX_VOLUME_SIZE                 MAX_128BIT_BLOCK_VOLUME_SIZE
#define MIN_FAT_VOLUME_SIZE				19456
#define MAX_FAT_VOLUME_SIZE				0x20000000000LL
#define MIN_NTFS_VOLUME_SIZE			2634752
#define OPTIMAL_MIN_NTFS_VOLUME_SIZE	(4 * BYTES_PER_GB)
#define MAX_NTFS_VOLUME_SIZE			(128LL * BYTES_PER_TB)	// NTFS volume can theoretically be up to 16 exabytes, but Windows XP and 2003 limit the size to that addressable with 32-bit clusters, i.e. max size is 128 TB (if 64-KB clusters are used).
#define MAX_HIDDEN_VOLUME_HOST_SIZE     MAX_NTFS_VOLUME_SIZE
#define MAX_HIDDEN_VOLUME_SIZE          ( MAX_HIDDEN_VOLUME_HOST_SIZE - HIDDEN_VOL_HEADER_OFFSET - HEADER_SIZE )
#define MIN_VOLUME_SIZE                 MIN_FAT_VOLUME_SIZE
#define MIN_HIDDEN_VOLUME_HOST_SIZE     ( MIN_VOLUME_SIZE * 2 + HIDDEN_VOL_HEADER_OFFSET + HEADER_SIZE )

#ifndef TC_NO_COMPILER_INT64
#if MAX_VOLUME_SIZE > MAX_VOLUME_SIZE_GENERAL
#error MAX_VOLUME_SIZE must be less than or equal to MAX_VOLUME_SIZE_GENERAL
#endif
#endif

#define TCalloc(X) calloc(1, X)
#define TCfree free

#define WIDE(x) (LPWSTR)L##x

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef uint8_t byte;
typedef uint16_t uint16;
typedef uint32_t uint32;

#ifdef TC_NO_COMPILER_INT64
typedef uint32_t	TC_LARGEST_COMPILER_UINT;
#else
typedef uint64_t	TC_LARGEST_COMPILER_UINT;
typedef int64_t int64;
typedef uint64_t uint64;
#endif

// Needed by Cryptolib
typedef uint8_t uint_8t;
typedef uint16_t uint_16t;
typedef uint32_t uint_32t;
#ifndef TC_NO_COMPILER_INT64
typedef uint64_t uint_64t;
#endif

typedef union
{
	struct
	{
		uint32_t LowPart;
		uint32_t HighPart;
	};
#ifndef TC_NO_COMPILER_INT64
	uint64_t Value;
#endif

} UINT64_STRUCT;


#define TC_THROW_FATAL_EXCEPTION	*(char *) 0 = 0

#define burn(mem,size) do { volatile char *burnm = (volatile char *)(mem); int burnc = size; while (burnc--) *burnm++ = 0; } while (0)

// The size of the memory area to wipe is in bytes amd it must be a multiple of 8.
#ifndef TC_NO_COMPILER_INT64
#	define FAST_ERASE64(mem,size) do { volatile uint64_t *burnm = (volatile uint64_t *)(mem); int burnc = size >> 3; while (burnc--) *burnm++ = 0; } while (0)
#else
#	define FAST_ERASE64(mem,size) do { volatile uint32_t *burnm = (volatile uint32_t *)(mem); int burnc = size >> 2; while (burnc--) *burnm++ = 0; } while (0)
#endif


#ifdef MAX_PATH
#define TC_MAX_PATH		MAX_PATH
#else
#define TC_MAX_PATH		260	/* Includes the null terminator */
#endif

#define MAX_URL_LENGTH	2084 /* Internet Explorer limit. Includes the terminating null character. */


enum
{
	/* WARNING: Add any new codes at the end (do NOT insert them between existing). Do NOT delete any
	existing codes. Changing these values or their meanings may cause incompatibility with other
	versions (for example, if a new version of the TrueCrypt installer receives an error code from
	an installed driver whose version is lower, it will interpret the error incorrectly). */

	ERR_SUCCESS = 0,
	ERR_OS_ERROR = 1,
	ERR_OUTOFMEMORY,
	ERR_PASSWORD_WRONG,
	ERR_VOL_FORMAT_BAD,
	ERR_DRIVE_NOT_FOUND,
	ERR_FILES_OPEN,
	ERR_VOL_SIZE_WRONG,
	ERR_COMPRESSION_NOT_SUPPORTED,
	ERR_PASSWORD_CHANGE_VOL_TYPE,
	ERR_PASSWORD_CHANGE_VOL_VERSION,
	ERR_VOL_SEEKING,
	ERR_VOL_WRITING,
	ERR_FILES_OPEN_LOCK,
	ERR_VOL_READING,
	ERR_DRIVER_VERSION,
	ERR_NEW_VERSION_REQUIRED,
	ERR_CIPHER_INIT_FAILURE,
	ERR_CIPHER_INIT_WEAK_KEY,
	ERR_SELF_TESTS_FAILED,
	ERR_SECTOR_SIZE_INCOMPATIBLE,
	ERR_VOL_ALREADY_MOUNTED,
	ERR_NO_FREE_DRIVES,
	ERR_FILE_OPEN_FAILED,
	ERR_VOL_MOUNT_FAILED,
	ERR_INVALID_DEVICE,
	ERR_ACCESS_DENIED,
	ERR_MODE_INIT_FAILED,
	ERR_DONT_REPORT,
	ERR_ENCRYPTION_NOT_COMPLETED,
	ERR_PARAMETER_INCORRECT
};

#endif 	// #ifndef TCDEFS_H
