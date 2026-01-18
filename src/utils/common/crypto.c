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

#include "crypto.h"
#include "xts.h"
#include "crc.h"
#include "../common/endian.h"
#include <string.h>

/* Update the following when adding a new cipher or EA:

   Crypto.h:
     ID #define
     MAX_EXPANDED_KEY #define

   Crypto.c:
     Ciphers[]
     EncryptionAlgorithms[]
     CipherInit()
     EncipherBlock()
     DecipherBlock()

*/

// Cipher configuration
static Cipher Ciphers[] =
{
//								Block Size	Key Size	Key Schedule Size
//	  ID		Name			(Bytes)		(Bytes)		(Bytes)
	{ AES256,		"AES256",	16,			32,			AES_KS				},
	{ AES192,		"AES192",	16,			24,			AES_KS				},
	{ AES128,		"AES128",	16,			16,			AES_KS				},
	{ BLOWFISH,	"Blowfish",		8,			56,			4168				},	// Deprecated/legacy
	{ CAST,		"CAST5",		8,			16,			128					},	// Deprecated/legacy
	{ DES56,	"DES",			8,			7,			128					},	// Deprecated/legacy
	{ SERPENT,	"Serpent",		16,			32,			140*4				},
	{ TRIPLEDES,"Triple DES",	8,			8*3,		128*3				},	// Deprecated/legacy
	{ TWOFISH,	"Twofish",		16,			32,			TWOFISH_KS			},
	{ 0,		0,				0,			0,			0					}
};


// Encryption algorithm configuration
// The following modes have been deprecated (legacy): LRW, CBC, INNER_CBC, OUTER_CBC
static EncryptionAlgorithm EncryptionAlgorithms[] =
{
	//  Cipher(s)                     Modes						FormatEnabled

	{ { 0,						0 }, { 0, 0, 0, 0 },				0 },	// Must be all-zero
	{ { AES256,					0 }, { XTS, LRW, CBC, 0 },			1 },
	{ { AES192,					0 }, { XTS, LRW, CBC, 0 },			1 },
	{ { AES128,					0 }, { XTS, LRW, CBC, 0 },			1 },
	{ { BLOWFISH,				0 }, { LRW, CBC, 0, 0 },			0 },	// Deprecated/legacy
	{ { CAST,					0 }, { LRW, CBC, 0, 0 },			0 },	// Deprecated/legacy
	{ { SERPENT,				0 }, { XTS, LRW, CBC, 0 },			1 },
	{ { TRIPLEDES,				0 }, { LRW, CBC, 0, 0 },			0 },	// Deprecated/legacy
	{ { TWOFISH,				0 }, { XTS, LRW, CBC, 0 },			1 },
	{ { TWOFISH, AES256,		0 }, { XTS, LRW, OUTER_CBC, 0 },	1 },
	{ { SERPENT, TWOFISH, AES256,	0 }, { XTS, LRW, OUTER_CBC, 0 },	1 },
	{ { AES256, SERPENT,		0 }, { XTS, LRW, OUTER_CBC, 0 },	1 },
	{ { AES256, TWOFISH, SERPENT,	0 }, { XTS, LRW, OUTER_CBC, 0 },	1 },
	{ { SERPENT, TWOFISH,		0 }, { XTS, LRW, OUTER_CBC, 0 },	1 },
	{ { BLOWFISH, AES256,		0 }, { INNER_CBC, 0, 0, 0 },		0 },	// Deprecated/legacy
	{ { SERPENT, BLOWFISH, AES256,	0 }, { INNER_CBC, 0, 0, 0 },		0 },	// Deprecated/legacy
	{ { 0,						0 }, { 0, 0, 0, 0 },				0 }		// Must be all-zero

};



// Hash algorithms
static Hash Hashes[] =
{	// ID			Name			Deprecated		System Encryption
	{ RIPEMD160,	"RIPEMD-160",	0,			1 },
	{ SHA512,		"SHA-512",		0,			0 },
	{ WHIRLPOOL,	"Whirlpool",	0,			0 },
	{ SHA1,			"SHA-1",		1,			0 },	// Deprecated/legacy
	{ 0, 0, 0 }
};

/* Return values: 0 = success, ERR_CIPHER_INIT_FAILURE (fatal), ERR_CIPHER_INIT_WEAK_KEY (non-fatal) */
int CipherInit (int cipher, unsigned char *key, uint8_t *ks)
{
	int retVal = ERR_SUCCESS;

	switch (cipher)
	{
	case AES256:
		if (aes_encrypt_key256 (key, (aes_encrypt_ctx *) ks) != EXIT_SUCCESS)
			return ERR_CIPHER_INIT_FAILURE;

		if (aes_decrypt_key256 (key, (aes_decrypt_ctx *) (ks + sizeof(aes_encrypt_ctx))) != EXIT_SUCCESS)
			return ERR_CIPHER_INIT_FAILURE;
		break;

	case AES192:
		if (aes_encrypt_key192 (key, (aes_encrypt_ctx *) ks) != EXIT_SUCCESS)
			return ERR_CIPHER_INIT_FAILURE;

		if (aes_decrypt_key192 (key, (aes_decrypt_ctx *) (ks + sizeof(aes_encrypt_ctx))) != EXIT_SUCCESS)
			return ERR_CIPHER_INIT_FAILURE;
		break;

	case AES128:
		if (aes_encrypt_key128 (key, (aes_encrypt_ctx *) ks) != EXIT_SUCCESS)
			return ERR_CIPHER_INIT_FAILURE;

		if (aes_decrypt_key128 (key, (aes_decrypt_ctx *) (ks + sizeof(aes_encrypt_ctx))) != EXIT_SUCCESS)
			return ERR_CIPHER_INIT_FAILURE;
		break;

	case SERPENT:
		serpent_set_key (key, CipherGetKeySize(SERPENT) * 8, ks);
		break;

	case TWOFISH:
		twofish_set_key ((TwofishInstance *)ks, (const u4byte *)key, CipherGetKeySize(TWOFISH) * 8);
		break;

	case BLOWFISH:
		/* Deprecated/legacy */
		BF_set_key ((BF_KEY *)ks, CipherGetKeySize(BLOWFISH), key);
		break;

	case DES56:
		/* Deprecated/legacy */
		switch (des_key_sched ((des_cblock *) key, (struct des_ks_struct *) ks))
		{
		case -1:
			return ERR_CIPHER_INIT_FAILURE;
		case -2:
			retVal = ERR_CIPHER_INIT_WEAK_KEY;		// Non-fatal error
			break;
		}
		break;

	case CAST:
		/* Deprecated/legacy */
		CAST_set_key((CAST_KEY *) ks, CipherGetKeySize(CAST), key);
		break;

	case TRIPLEDES:
		/* Deprecated/legacy */
		switch (des_key_sched ((des_cblock *) key, (struct des_ks_struct *) ks))
		{
		case -1:
			return ERR_CIPHER_INIT_FAILURE;
		case -2:
			retVal = ERR_CIPHER_INIT_WEAK_KEY;		// Non-fatal error
			break;
		}
		switch (des_key_sched ((des_cblock *) ((char*)(key)+8), (struct des_ks_struct *) (ks + CipherGetKeyScheduleSize (DES56))))
		{
		case -1:
			return ERR_CIPHER_INIT_FAILURE;
		case -2:
			retVal = ERR_CIPHER_INIT_WEAK_KEY;		// Non-fatal error
			break;
		}
		switch (des_key_sched ((des_cblock *) ((char*)(key)+16), (struct des_ks_struct *) (ks + CipherGetKeyScheduleSize (DES56) * 2)))
		{
		case -1:
			return ERR_CIPHER_INIT_FAILURE;
		case -2:
			retVal = ERR_CIPHER_INIT_WEAK_KEY;		// Non-fatal error
			break;
		}

		// Verify whether all three DES keys are mutually different
		if (((*((int64_t *) key) ^ *((int64_t *) key+1)) & 0xFEFEFEFEFEFEFEFEULL) == 0
		|| ((*((int64_t *) key+1) ^ *((int64_t *) key+2)) & 0xFEFEFEFEFEFEFEFEULL) == 0
		|| ((*((int64_t *) key) ^ *((int64_t *) key+2)) & 0xFEFEFEFEFEFEFEFEULL) == 0)
			retVal = ERR_CIPHER_INIT_WEAK_KEY;		// Non-fatal error

		break;

	default:
		// Unknown/wrong cipher ID
		return ERR_CIPHER_INIT_FAILURE;
	}

	return retVal;
}

void EncipherBlock(int cipher, void *data, void *ks)
{
	switch (cipher)
	{
	case AES256:
	case AES192:
	case AES128:
			aes_encrypt (data, data, ks); break;
	case TWOFISH:		twofish_encrypt (ks, data, data); break;
	case SERPENT:		serpent_encrypt (data, data, ks); break;
	case BLOWFISH:		BF_ecb_le_encrypt (data, data, ks, 1); break;	// Deprecated/legacy
	case DES56:			des_encrypt (data, ks, 1); break;				// Deprecated/legacy
	case CAST:			CAST_ecb_encrypt (data, data, ks, 1); break;	// Deprecated/legacy
	case TRIPLEDES:		des_ecb3_encrypt (data, data, ks,				// Deprecated/legacy
						(void*)((char*) ks + CipherGetKeyScheduleSize (DES56)), (void*)((char*) ks + CipherGetKeyScheduleSize (DES56) * 2), 1); break;
	default:			fatal("EncipherBlock(): Unknown/wrong ID\n");	// Unknown/wrong ID
	}
}

void DecipherBlock(int cipher, void *data, void *ks)
{
	switch (cipher)
	{
	case SERPENT:	serpent_decrypt (data, data, ks); break;
	case TWOFISH:	twofish_decrypt (ks, data, data); break;
	case AES256:
	case AES192:
	case AES128:
			aes_decrypt (data, data, (void *) ((char *) ks + sizeof(aes_encrypt_ctx))); break;
	case BLOWFISH:	BF_ecb_le_encrypt (data, data, ks, 0); break;	// Deprecated/legacy
	case DES56:		des_encrypt (data, ks, 0); break;				// Deprecated/legacy
	case CAST:		CAST_ecb_encrypt (data, data, ks,0); break;		// Deprecated/legacy
	case TRIPLEDES:	des_ecb3_encrypt (data, data, ks,				// Deprecated/legacy
					(void*)((char*) ks + CipherGetKeyScheduleSize (DES56)),
					(void*)((char*) ks + CipherGetKeyScheduleSize (DES56) * 2), 0); break;
	default:		fatal("DecipherBlock(): Unknown/wrong ID\n");	// Unknown/wrong ID
	}
}

// Ciphers support

Cipher *CipherGet (int id)
{
	int i;
	for (i = 0; Ciphers[i].Id != 0; i++)
		if (Ciphers[i].Id == id)
			return &Ciphers[i];

	return NULL;
}

char *CipherGetName (int cipherId)
{
	return CipherGet (cipherId) -> Name;
}

int CipherGetBlockSize (int cipherId)
{
	return CipherGet (cipherId) -> BlockSize;
}

int CipherGetKeySize (int cipherId)
{
	return CipherGet (cipherId) -> KeySize;
}

int CipherGetKeyScheduleSize (int cipherId)
{
	return CipherGet (cipherId) -> KeyScheduleSize;
}


// Encryption algorithms support

int EAGetFirst (void)
{
	return 1;
}

// Returns number of EAs
int EAGetCount (void)
{
	int ea, count = 0;

	for (ea = EAGetFirst (); ea != 0; ea = EAGetNext (ea))
	{
		count++;
	}
	return count;
}

int EAGetNext (int previousEA)
{
	int id = previousEA + 1;
	if (EncryptionAlgorithms[id].Ciphers[0] != 0) return id;
	return 0;
}


// Return values: 0 = success, ERR_CIPHER_INIT_FAILURE (fatal), ERR_CIPHER_INIT_WEAK_KEY (non-fatal)
int EAInit (int ea, unsigned char *key, uint8_t *ks)
{
	int c, retVal = ERR_SUCCESS;

	if (ea == 0)
		return ERR_CIPHER_INIT_FAILURE;

	for (c = EAGetFirstCipher (ea); c != 0; c = EAGetNextCipher (ea, c))
	{
		switch (CipherInit (c, key, ks))
		{
		case ERR_CIPHER_INIT_FAILURE:
			return ERR_CIPHER_INIT_FAILURE;

		case ERR_CIPHER_INIT_WEAK_KEY:
			retVal = ERR_CIPHER_INIT_WEAK_KEY;		// Non-fatal error
			break;
		}

		key += CipherGetKeySize (c);
		ks += CipherGetKeyScheduleSize (c);
	}
	return retVal;
}


int EAInitMode (PCRYPTO_INFO ci)
{
	switch (ci->mode)
	{
	case XTS:
		// Secondary key schedule
		if (EAInit (ci->ea, ci->k2, ci->ks2) != ERR_SUCCESS)
			return 0;

		/* Note: XTS mode could potentially be initialized with a weak key causing all blocks in one data unit
		on the volume to be tweaked with zero tweaks (i.e. 512 bytes of the volume would be encrypted in ECB
		mode). However, to create a TrueCrypt volume with such a weak key, each human being on Earth would have
		to create approximately 11,378,125,361,078,862 (about eleven quadrillion) TrueCrypt volumes (provided
		that the size of each of the volumes is 1024 terabytes). */
		break;

	case LRW:
		switch (CipherGetBlockSize (EAGetFirstCipher (ci->ea)))
		{
		case 8:
			/* Deprecated/legacy */
			return Gf64TabInit (ci->k2, &ci->gf_ctx);

		case 16:
			return Gf128Tab64Init (ci->k2, &ci->gf_ctx);

		default:
			fatal("EAInitMode(): Fatal exception\n");
		}

		break;

	case CBC:
	case INNER_CBC:
	case OUTER_CBC:
		// The mode does not need to be initialized or is initialized elsewhere
		return 1;

	default:
		// Unknown/wrong ID
		fatal("EAInitMode(): Unknown/wrong ID\n");
	}
	return 1;
}


// Returns name of EA, cascaded cipher names are separated by hyphens
char *EAGetName (char *buf, int ea)
{
	int i = EAGetLastCipher(ea);
	strcpy (buf, (i != 0) ? CipherGetName (i) : "?");

	while ((i = EAGetPreviousCipher(ea, i)))
	{
		strcat (buf, "-");
		strcat (buf, CipherGetName (i));
	}

	return buf;
}


int EAGetByName (char *name)
{
	int ea = EAGetFirst ();
	char n[128];

	do
	{
		EAGetName (n, ea);
		if (strcmp (n, name) == 0)
			return ea;
	}
	while ((ea = EAGetNext (ea)));

	return 0;
}

// Returns sum of key sizes of all ciphers of the EA (in bytes)
int EAGetKeySize (int ea)
{
	int i = EAGetFirstCipher (ea);
	int size = CipherGetKeySize (i);

	while ((i = EAGetNextCipher (ea, i)))
	{
		size += CipherGetKeySize (i);
	}

	return size;
}


// Returns the first mode of operation of EA
int EAGetFirstMode (int ea)
{
	return (EncryptionAlgorithms[ea].Modes[0]);
}


int EAGetNextMode (int ea, int previousModeId)
{
	int c, i = 0;
	while ((c = EncryptionAlgorithms[ea].Modes[i++]))
	{
		if (c == previousModeId)
			return EncryptionAlgorithms[ea].Modes[i];
	}

	return 0;
}


// Returns the name of the mode of operation of the whole EA
char *EAGetModeName (int ea, int mode, int capitalLetters)
{
	switch (mode)
	{
	case XTS:

		return "XTS";

	case LRW:

		/* Deprecated/legacy */

		return "LRW";

	case CBC:
		{
			/* Deprecated/legacy */

			char eaName[100];
			EAGetName (eaName, ea);

			if (strcmp (eaName, "Triple DES") == 0)
				return capitalLetters ? "Outer-CBC" : "outer-CBC";

			return "CBC";
		}

	case OUTER_CBC:

		/* Deprecated/legacy */

		return  capitalLetters ? "Outer-CBC" : "outer-CBC";

	case INNER_CBC:

		/* Deprecated/legacy */

		return capitalLetters ? "Inner-CBC" : "inner-CBC";

	}
	return "[unknown]";
}



// Returns sum of key schedule sizes of all ciphers of the EA
int EAGetKeyScheduleSize (int ea)
{
	int i = EAGetFirstCipher(ea);
	int size = CipherGetKeyScheduleSize (i);

	while ((i = EAGetNextCipher(ea, i)))
	{
		size += CipherGetKeyScheduleSize (i);
	}

	return size;
}


// Returns the largest key size needed by an EA for the specified mode of operation
int EAGetLargestKeyForMode (int mode)
{
	int ea, key = 0;

	for (ea = EAGetFirst (); ea != 0; ea = EAGetNext (ea))
	{
		if (!EAIsModeSupported (ea, mode))
			continue;

		if (EAGetKeySize (ea) >= key)
			key = EAGetKeySize (ea);
	}
	return key;
}


// Returns the largest key needed by any EA for any mode
int EAGetLargestKey (void)
{
	int ea, key = 0;

	for (ea = EAGetFirst (); ea != 0; ea = EAGetNext (ea))
	{
		if (EAGetKeySize (ea) >= key)
			key = EAGetKeySize (ea);
	}

	return key;
}


// Returns number of ciphers in EA
int EAGetCipherCount (int ea)
{
	int i = 0;
	while (EncryptionAlgorithms[ea].Ciphers[i++]);

	return i - 1;
}


int EAGetFirstCipher (int ea)
{
	return EncryptionAlgorithms[ea].Ciphers[0];
}


int EAGetLastCipher (int ea)
{
	int c, i = 0;
	while ((c = EncryptionAlgorithms[ea].Ciphers[i++]));

	return EncryptionAlgorithms[ea].Ciphers[i - 2];
}


int EAGetNextCipher (int ea, int previousCipherId)
{
	int c, i = 0;
	while ((c = EncryptionAlgorithms[ea].Ciphers[i++]))
	{
		if (c == previousCipherId)
			return EncryptionAlgorithms[ea].Ciphers[i];
	}

	return 0;
}


int EAGetPreviousCipher (int ea, int previousCipherId)
{
	int c, i = 0;

	if (EncryptionAlgorithms[ea].Ciphers[i++] == previousCipherId)
		return 0;

	while ((c = EncryptionAlgorithms[ea].Ciphers[i++]))
	{
		if (c == previousCipherId)
			return EncryptionAlgorithms[ea].Ciphers[i - 2];
	}

	return 0;
}


int EAIsFormatEnabled (int ea)
{
	return EncryptionAlgorithms[ea].FormatEnabled;
}


// Returns TRUE if the mode of operation is supported for the encryption algorithm
int EAIsModeSupported (int ea, int testedMode)
{
	int mode;

	for (mode = EAGetFirstMode (ea); mode != 0; mode = EAGetNextMode (ea, mode))
	{
		if (mode == testedMode)
			return 1;
	}
	return 0;
}


Hash *HashGet (int id)
{
	int i;
	for (i = 0; Hashes[i].Id != 0; i++)
		if (Hashes[i].Id == id)
			return &Hashes[i];

	return 0;
}


int HashGetIdByName (char *name)
{
	int i;
	for (i = 0; Hashes[i].Id != 0; i++)
		if (strcmp (Hashes[i].Name, name) == 0)
			return Hashes[i].Id;

	return 0;
}


char *HashGetName (int hashId)
{
	return HashGet (hashId) -> Name;
}


int HashIsDeprecated (int hashId)
{
	return HashGet (hashId) -> Deprecated;
}


PCRYPTO_INFO crypto_open (void)
{
	/* Do the crt allocation */
	PCRYPTO_INFO cryptoInfo = (PCRYPTO_INFO) malloc (sizeof (CRYPTO_INFO));
	memset (cryptoInfo, 0, sizeof (CRYPTO_INFO));

	if (cryptoInfo == NULL)
		return NULL;

	cryptoInfo->ea = -1;
	return cryptoInfo;
}

void crypto_loadkey (PKEY_INFO keyInfo, char *lpszUserKey, int nUserKeyLen)
{
	keyInfo->keyLength = nUserKeyLen;
	memset (keyInfo->userKey, 0x00, sizeof (keyInfo->userKey));
	memcpy (keyInfo->userKey, lpszUserKey, nUserKeyLen);
}

void crypto_close (PCRYPTO_INFO cryptoInfo)
{
	if (cryptoInfo != NULL)
		free (cryptoInfo);
}


void Xor128 (uint64_t *a, uint64_t *b)
{
	*a++ ^= *b++;
	*a ^= *b;
}


void Xor64 (uint64_t *a, uint64_t *b)
{
	*a ^= *b;
}


void EncryptBufferLRW128 (uint8_t *buffer, uint64_t length, uint64_t blockIndex, PCRYPTO_INFO cryptoInfo)
{
	/* Deprecated/legacy */

	int cipher = EAGetFirstCipher (cryptoInfo->ea);
	int cipherCount = EAGetCipherCount (cryptoInfo->ea);
	uint8_t *p = buffer;
	uint8_t *ks = cryptoInfo->ks;
	uint8_t i[8];
	uint8_t t[16];
	uint64_t b;

	*(uint64_t *)i = BE64(blockIndex);

	if (length % 16)
		fatal("EncryptBufferLRW128(): Length not divisible by 16\n");

	// Note that the maximum supported volume size is 8589934592 GB  (i.e., 2^63 bytes).

	for (b = 0; b < length >> 4; b++)
	{
		Gf128MulBy64Tab (i, t, &cryptoInfo->gf_ctx);
		Xor128 ((uint64_t *)p, (uint64_t *)t);

		if (cipherCount > 1)
		{
			// Cipher cascade
			for (cipher = EAGetFirstCipher (cryptoInfo->ea);
				cipher != 0;
				cipher = EAGetNextCipher (cryptoInfo->ea, cipher))
			{
				EncipherBlock (cipher, p, ks);
				ks += CipherGetKeyScheduleSize (cipher);
			}
			ks = cryptoInfo->ks;
		}
		else
		{
			EncipherBlock (cipher, p, ks);
		}

		Xor128 ((uint64_t *)p, (uint64_t *)t);

		p += 16;

		if (i[7] != 0xff)
			i[7]++;
		else
			*(uint64_t *)i = BE64 ( BE64(*(uint64_t *)i) + 1 );
	}

	memset (t, 0x00, sizeof(t));
}


void EncryptBufferLRW64 (uint8_t *buffer, uint64_t length, uint64_t blockIndex, PCRYPTO_INFO cryptoInfo)
{
	/* Deprecated/legacy */

	int cipher = EAGetFirstCipher (cryptoInfo->ea);
	uint8_t *p = buffer;
	uint8_t *ks = cryptoInfo->ks;
	uint8_t i[8];
	uint8_t t[8];
	uint64_t b;

	*(uint64_t *)i = BE64(blockIndex);

	if (length % 8)
		fatal("EncryptBufferLRW64(): Length not divisible by 8\n");

	for (b = 0; b < length >> 3; b++)
	{
		Gf64MulTab (i, t, &cryptoInfo->gf_ctx);
		Xor64 ((uint64_t *)p, (uint64_t *)t);

		EncipherBlock (cipher, p, ks);

		Xor64 ((uint64_t *)p, (uint64_t *)t);

		p += 8;

		if (i[7] != 0xff)
			i[7]++;
		else
			*(uint64_t *)i = BE64 ( BE64(*(uint64_t *)i) + 1 );
	}

	memset (t, 0x00, sizeof(t));
}


void DecryptBufferLRW128 (uint8_t *buffer, uint64_t length, uint64_t blockIndex, PCRYPTO_INFO cryptoInfo)
{
	/* Deprecated/legacy */

	int cipher = EAGetFirstCipher (cryptoInfo->ea);
	int cipherCount = EAGetCipherCount (cryptoInfo->ea);
	uint8_t *p = buffer;
	uint8_t *ks = cryptoInfo->ks;
	uint8_t i[8];
	uint8_t t[16];
	uint64_t b;

	*(uint64_t *)i = BE64(blockIndex);

	if (length % 16)
		fatal("DecryptufferLRW128(): Length not divisible by 16\n");

	// Note that the maximum supported volume size is 8589934592 GB  (i.e., 2^63 bytes).

	for (b = 0; b < length >> 4; b++)
	{
		Gf128MulBy64Tab (i, t, &cryptoInfo->gf_ctx);
		Xor128 ((uint64_t *)p, (uint64_t *)t);

		if (cipherCount > 1)
		{
			// Cipher cascade
			ks = cryptoInfo->ks + EAGetKeyScheduleSize (cryptoInfo->ea);

			for (cipher = EAGetLastCipher (cryptoInfo->ea);
				cipher != 0;
				cipher = EAGetPreviousCipher (cryptoInfo->ea, cipher))
			{
				ks -= CipherGetKeyScheduleSize (cipher);
				DecipherBlock (cipher, p, ks);
			}
		}
		else
		{
			DecipherBlock (cipher, p, ks);
		}

		Xor128 ((uint64_t *)p, (uint64_t *)t);

		p += 16;

		if (i[7] != 0xff)
			i[7]++;
		else
			*(uint64_t *)i = BE64 ( BE64(*(uint64_t *)i) + 1 );
	}

	memset (t, 0x00, sizeof(t));
}



void DecryptBufferLRW64 (uint8_t *buffer, uint64_t length, uint64_t blockIndex, PCRYPTO_INFO cryptoInfo)
{
	/* Deprecated/legacy */

	int cipher = EAGetFirstCipher (cryptoInfo->ea);
	uint8_t *p = buffer;
	uint8_t *ks = cryptoInfo->ks;
	uint8_t i[8];
	uint8_t t[8];
	uint64_t b;

	*(uint64_t *)i = BE64(blockIndex);

	if (length % 8)
		fatal("DecryptufferLRW64(): Length not divisible by 64\n");

	for (b = 0; b < length >> 3; b++)
	{
		Gf64MulTab (i, t, &cryptoInfo->gf_ctx);
		Xor64 ((uint64_t *)p, (uint64_t *)t);

		DecipherBlock (cipher, p, ks);

		Xor64 ((uint64_t *)p, (uint64_t *)t);

		p += 8;

		if (i[7] != 0xff)
			i[7]++;
		else
			*(uint64_t *)i = BE64 ( BE64(*(uint64_t *)i) + 1 );
	}

	memset (t, 0x00, sizeof(t));
}


// Initializes IV and whitening values for sector encryption/decryption in CBC mode.
// IMPORTANT: This function has been deprecated (legacy).
void
InitSectorIVAndWhitening (uint64_t unitNo,
	int blockSize,
	uint32_t *iv,
	uint64_t *ivSeed,
	uint32_t *whitening)
{

	/* IMPORTANT: This function has been deprecated (legacy) */

	uint64_t iv64[4];
	uint32_t *iv32 = (uint32_t *) iv64;

	iv64[0] = ivSeed[0] ^ LE64(unitNo);
	iv64[1] = ivSeed[1] ^ LE64(unitNo);
	iv64[2] = ivSeed[2] ^ LE64(unitNo);
	if (blockSize == 16)
	{
		iv64[3] = ivSeed[3] ^ LE64(unitNo);
	}

	iv[0] = iv32[0];
	iv[1] = iv32[1];

	switch (blockSize)
	{
	case 16:

		// 128-bit block

		iv[2] = iv32[2];
		iv[3] = iv32[3];

		whitening[0] = LE32( crc32int ( &iv32[4] ) ^ crc32int ( &iv32[7] ) );
		whitening[1] = LE32( crc32int ( &iv32[5] ) ^ crc32int ( &iv32[6] ) );
		break;

	case 8:

		// 64-bit block

		whitening[0] = LE32( crc32int ( &iv32[2] ) ^ crc32int ( &iv32[5] ) );
		whitening[1] = LE32( crc32int ( &iv32[3] ) ^ crc32int ( &iv32[4] ) );
		break;

	default:
		fatal("InitSectorIVAndWhitening(): Invalid length: %i\n", blockSize);
	}
}


// EncryptBufferCBC    (deprecated/legacy)
//
// data:		data to be encrypted
// len:			number of bytes to encrypt (must be divisible by the largest cipher block size)
// ks:			scheduled key
// iv:			IV
// whitening:	whitening constants
// ea:			outer-CBC cascade ID (0 = CBC/inner-CBC)
// cipher:		CBC/inner-CBC cipher ID (0 = outer-CBC)

static void
EncryptBufferCBC (uint32_t *data,
		 unsigned int len,
		 uint8_t *ks,
		 uint32_t *iv,
		 uint32_t *whitening,
		 int ea,
		 int cipher)
{
	/* IMPORTANT: This function has been deprecated (legacy) */

	uint32_t bufIV[4] = { 0 };
	uint64_t i;
	int blockSize = CipherGetBlockSize (ea != 0 ? EAGetFirstCipher (ea) : cipher);

	if (len % blockSize)
		fatal("EncryptBufferCBC(): Length not divisible by %i\n", blockSize);

	//  IV
	bufIV[0] = iv[0];
	bufIV[1] = iv[1];
	if (blockSize == 16)
	{
		bufIV[2] = iv[2];
		bufIV[3] = iv[3];
	}

	// Encrypt each block
	for (i = 0; i < len/blockSize; i++)
	{
		// CBC
		data[0] ^= bufIV[0];
		data[1] ^= bufIV[1];
		if (blockSize == 16)
		{
			data[2] ^= bufIV[2];
			data[3] ^= bufIV[3];
		}

		if (ea != 0)
		{
			// Outer-CBC
			for (cipher = EAGetFirstCipher (ea); cipher != 0; cipher = EAGetNextCipher (ea, cipher))
			{
				EncipherBlock (cipher, data, ks);
				ks += CipherGetKeyScheduleSize (cipher);
			}
			ks -= EAGetKeyScheduleSize (ea);
		}
		else
		{
			// CBC/inner-CBC
			EncipherBlock (cipher, data, ks);
		}

		// CBC
		bufIV[0] = data[0];
		bufIV[1] = data[1];
		if (blockSize == 16)
		{
			bufIV[2] = data[2];
			bufIV[3] = data[3];
		}

		// Whitening
		data[0] ^= whitening[0];
		data[1] ^= whitening[1];
		if (blockSize == 16)
		{
			data[2] ^= whitening[0];
			data[3] ^= whitening[1];
		}

		data += blockSize / sizeof(*data);
	}
}


// DecryptBufferCBC  (deprecated/legacy)
//
// data:		data to be decrypted
// len:			number of bytes to decrypt (must be divisible by the largest cipher block size)
// ks:			scheduled key
// iv:			IV
// whitening:	whitening constants
// ea:			outer-CBC cascade ID (0 = CBC/inner-CBC)
// cipher:		CBC/inner-CBC cipher ID (0 = outer-CBC)

static void
DecryptBufferCBC (uint32_t *data,
		 unsigned int len,
		 uint8_t *ks,
		 uint32_t *iv,
 		 uint32_t *whitening,
		 int ea,
		 int cipher)
{

	/* IMPORTANT: This function has been deprecated (legacy) */

	uint32_t bufIV[4] = { 0 };
	uint64_t i;
	uint32_t ct[4];
	int blockSize = CipherGetBlockSize (ea != 0 ? EAGetFirstCipher (ea) : cipher);

	if (len % blockSize)
		fatal("DecryptBufferCBC(): Length not divisible by %i\n", blockSize);

	//  IV
	bufIV[0] = iv[0];
	bufIV[1] = iv[1];
	if (blockSize == 16)
	{
		bufIV[2] = iv[2];
		bufIV[3] = iv[3];
	}

	// Decrypt each block
	for (i = 0; i < len/blockSize; i++)
	{
		// Dewhitening
		if (whitening)
		{
			data[0] ^= whitening[0];
			data[1] ^= whitening[1];
			if (blockSize == 16)
			{
				data[2] ^= whitening[0];
				data[3] ^= whitening[1];
			}

			// CBC
			ct[0] = data[0];
			ct[1] = data[1];
			if (blockSize == 16)
			{
				ct[2] = data[2];
				ct[3] = data[3];
			}
		}

		if (ea != 0)
		{
			// Outer-CBC
			ks += EAGetKeyScheduleSize (ea);
			for (cipher = EAGetLastCipher (ea); cipher != 0; cipher = EAGetPreviousCipher (ea, cipher))
			{
				ks -= CipherGetKeyScheduleSize (cipher);
				DecipherBlock (cipher, data, ks);
			}
		}
		else
		{
			// CBC/inner-CBC
			DecipherBlock (cipher, data, ks);
		}

		// CBC
		data[0] ^= bufIV[0];
		data[1] ^= bufIV[1];
		if (whitening)
		{
			bufIV[0] = ct[0];
			bufIV[1] = ct[1];
		}
		if (blockSize == 16)
		{
			data[2] ^= bufIV[2];
			data[3] ^= bufIV[3];
			if (whitening)
			{
				bufIV[2] = ct[2];
				bufIV[3] = ct[3];
			}
		}

		data += blockSize / sizeof(*data);
	}
}


// EncryptBuffer
//
// buf:			data to be encrypted
// len:			number of bytes to encrypt; must be divisible by the block size (for cascaded
//              ciphers divisible by the largest block size used within the cascade)
void EncryptBuffer (uint8_t *buf,
			   uint64_t len,
			   PCRYPTO_INFO cryptoInfo)
{
	switch (cryptoInfo->mode)
	{
	case XTS:
		{
			uint8_t *ks = cryptoInfo->ks;
			uint8_t *ks2 = cryptoInfo->ks2;
			UINT64_STRUCT dataUnitNo;
			int cipher;

			// When encrypting/decrypting a buffer (typically a volume header) the sequential number
			// of the first XTS data unit in the buffer is always 0 and the start of the buffer is
			// always considered aligned with the start of a data unit.
			dataUnitNo.LowPart = 0;
			dataUnitNo.HighPart = 0;

			for (cipher = EAGetFirstCipher (cryptoInfo->ea);
				cipher != 0;
				cipher = EAGetNextCipher (cryptoInfo->ea, cipher))
			{
				EncryptBufferXTS (buf, len, &dataUnitNo, 0, ks, ks2, cipher);

				ks += CipherGetKeyScheduleSize (cipher);
				ks2 += CipherGetKeyScheduleSize (cipher);
			}
		}
		break;

	case LRW:

		/* Deprecated/legacy */

		switch (CipherGetBlockSize (EAGetFirstCipher (cryptoInfo->ea)))
		{
		case 8:
			EncryptBufferLRW64 ((uint8_t *)buf, (uint64_t) len, 1, cryptoInfo);
			break;

		case 16:
			EncryptBufferLRW128 ((uint8_t *)buf, (uint64_t) len, 1, cryptoInfo);
			break;

		default:
			fatal("EncryptBuffer(): Invalid length: %i\n", CipherGetBlockSize (EAGetFirstCipher (cryptoInfo->ea)));
		}
		break;

	case CBC:
	case INNER_CBC:
		{
			/* Deprecated/legacy */

			uint8_t *ks = cryptoInfo->ks;
			int cipher;

			for (cipher = EAGetFirstCipher (cryptoInfo->ea);
				cipher != 0;
				cipher = EAGetNextCipher (cryptoInfo->ea, cipher))
			{
				EncryptBufferCBC ((uint32_t *) buf,
					(unsigned int) len,
					ks,
					(uint32_t *) cryptoInfo->k2,
					(uint32_t *) &cryptoInfo->k2[8],
					0,
					cipher);

				ks += CipherGetKeyScheduleSize (cipher);
			}
		}
		break;

	case OUTER_CBC:

		/* Deprecated/legacy */

		EncryptBufferCBC ((uint32_t *) buf,
			(unsigned int) len,
			cryptoInfo->ks,
			(uint32_t *) cryptoInfo->k2,
			(uint32_t *) &cryptoInfo->k2[8],
			cryptoInfo->ea,
			0);

		break;

	default:
		// Unknown/wrong ID
		fatal("EncryptBuffer(): Unknown/wrong ID\n");
	}
}

// DecryptBuffer
//
// buf:			data to be decrypted
// len:			number of bytes to decrypt; must be divisible by the block size (for cascaded
//              ciphers divisible by the largest block size used within the cascade)
void DecryptBuffer (uint8_t *buf,
	uint64_t len,
	uint32_t secSz,
	uint64_t secN,
	uint8_t flags,
	PCRYPTO_INFO cryptoInfo)
{

	void *iv = cryptoInfo->k2;
	uint32_t sectorIV[4];
	uint32_t secWhitening[2];
	uint64_t *iv64 = (uint64_t *) iv;

	switch (cryptoInfo->mode)
	{
	case XTS:
		{
			uint8_t *ks = cryptoInfo->ks + EAGetKeyScheduleSize (cryptoInfo->ea);
			uint8_t *ks2 = cryptoInfo->ks2 + EAGetKeyScheduleSize (cryptoInfo->ea);
			UINT64_STRUCT dataUnitNo;
			int cipher;

			// When encrypting/decrypting a buffer (typically a volume header) the sequential number
			// of the first XTS data unit in the buffer is always 0 and the start of the buffer is
			// always considered aligned with the start of the data unit 0.
			dataUnitNo.Value = secN;

			for (cipher = EAGetLastCipher (cryptoInfo->ea);
				cipher != 0;
				cipher = EAGetPreviousCipher (cryptoInfo->ea, cipher))
			{
				ks -= CipherGetKeyScheduleSize (cipher);
				ks2 -= CipherGetKeyScheduleSize (cipher);
				DecryptBufferXTS (buf, len, &dataUnitNo, 0, ks, ks2, cipher);
			}
		}
		break;

	case LRW:
	{
		uint32_t n = 0;
		if (flags & 2)
			n = secN;

		switch (CipherGetBlockSize (EAGetFirstCipher (cryptoInfo->ea)))
		{
		case 8:
			DecryptBufferLRW64 (buf, (uint64_t) len, 1 + (secSz / 8) * n, cryptoInfo);
			break;

		case 16:
			DecryptBufferLRW128 (buf, (uint64_t) len, 1 + (secSz / 16) * n, cryptoInfo);
			break;

		default:
			fatal("DecryptBuffer(): Invalid length: %i\n", CipherGetBlockSize (EAGetFirstCipher (cryptoInfo->ea)));
		}
		break;
	}

	case CBC:
	case INNER_CBC:
		{
			/* Deprecated/legacy */

			uint8_t *ks = cryptoInfo->ks + EAGetKeyScheduleSize (cryptoInfo->ea);
			int cipher;
			for (cipher = EAGetLastCipher (cryptoInfo->ea);
				cipher != 0;
				cipher = EAGetPreviousCipher (cryptoInfo->ea, cipher))
			{
				ks -= CipherGetKeyScheduleSize (cipher);

				if (flags & 1)
				{
					DecryptBufferCBC ((uint32_t *) buf,
						(unsigned int) len,
						ks,
						(uint32_t *) cryptoInfo->k2,
						NULL,
						0,
						cipher);
				}
				else if (flags & 2)
				{
					InitSectorIVAndWhitening (secN, CipherGetBlockSize (cipher), sectorIV, iv64, secWhitening);
					DecryptBufferCBC ((uint32_t *) buf,
						(unsigned int) len,
						cryptoInfo->ks,
						sectorIV,
						secWhitening,
						0,
						cipher);
				}
				else
				{
					DecryptBufferCBC ((uint32_t *) buf,
						(unsigned int) len,
						ks,
						(uint32_t *) cryptoInfo->k2,
						(uint32_t *) &cryptoInfo->k2[8],
						0,
						cipher);
				}
			}
		}
		break;

	case OUTER_CBC:
		if (flags & 1)
		{
			DecryptBufferCBC ((uint32_t *) buf,
				(unsigned int) len,
				cryptoInfo->ks,
				(uint32_t *) cryptoInfo->k2,
				NULL,
				cryptoInfo->ea,
				0);
		}
		else if (flags & 2)
		{
			InitSectorIVAndWhitening (secN, CipherGetBlockSize (EAGetFirstCipher (cryptoInfo->ea)), sectorIV, iv64, secWhitening);
			DecryptBufferCBC ((uint32_t *) buf,
				(unsigned int) len,
				cryptoInfo->ks,
				sectorIV,
				secWhitening,
				cryptoInfo->ea,
				0);
		}
		else
		{
			DecryptBufferCBC ((uint32_t *) buf,
				(unsigned int) len,
				cryptoInfo->ks,
				(uint32_t *) cryptoInfo->k2,
				(uint32_t *) &cryptoInfo->k2[8],
				cryptoInfo->ea,
				0);
		}

		break;

	default:
		// Unknown/wrong ID
		fatal("DecryptBuffer(): Unknown/wrong ID\n");
	}
}

// Returns the maximum number of bytes necessary to be generated by the PBKDF2 (PKCS #5)
int GetMaxPkcs5OutSize (void)
{
	int size = 32;

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

	size = max (size, EAGetLargestKeyForMode (XTS) * 2);	// Sizes of primary + secondary keys

	size = max (size, LEGACY_VOL_IV_SIZE + EAGetLargestKeyForMode (LRW));		// Deprecated/legacy
	size = max (size, LEGACY_VOL_IV_SIZE + EAGetLargestKeyForMode (CBC));		// Deprecated/legacy
	size = max (size, LEGACY_VOL_IV_SIZE + EAGetLargestKeyForMode (OUTER_CBC));	// Deprecated/legacy
	size = max (size, LEGACY_VOL_IV_SIZE + EAGetLargestKeyForMode (INNER_CBC));	// Deprecated/legacy

	return size;
}
