#include "mds.h"
#include "edc.h"


#include "common/crypto.h"
#include "common/endian.h"
#include "common/pkcs5.h"
#include "common/crc.h"

#include <string.h>

#define byte   uint8_t
#define uint64 uint64_t

typedef union {
    struct {
        uint32_t LowPart;
        uint32_t HighPart;
    };
    uint64_t Value;
} UINT64_STRUCT;

void unshuffle1(u8 *data)
{
    u32 val = getEDC(data, 0x40) ^ 0x567372ff;
    for(int i = 0; i < 0x40; i += 4)
    {
        val = (val * 0x35e85a6d) + 0x1548dce9;
        u32 ud = getU32(data + i);
        setU32(data + i, ud ^ val ^ 0xec564717);

        if (data[i] == 0)
            data[i] = 0x5f;
        if (data[i+1] == 0)
            data[i+1] = 0x5f;
        if (data[i+2] == 0)
            data[i+2] = 0x5f;
        if (data[i+3] == 0)
            data[i+3] = 0x5f;
    }
}

void DecryptBlock(u8 *buf,
	uint64_t len,
	u32 secSz,
	u64 secN,
	u8 flags,
	PCRYPTO_INFO cryptoInfo)
{
    const int blockSize = CipherGetBlockSize( EAGetFirstCipher(cryptoInfo->ea) );

    u64 blk = 0x200;
    if (blockSize <= secSz)
        blk = secSz;

    const u64 asz = blk - (blk % blockSize);
    if ((flags & 4) == 0)
    {
        const u32 c = len / blk;
        for (int i = 0; i < c; i++)
        {
            DecryptBuffer(buf + i * blk, asz, asz, secN + i, flags, cryptoInfo);
        }
    }
    else
    {
        u32 adsz = len - (len % blockSize);
        u64 pos = 0;
        int i = 0;
        while(adsz > 0)
        {
            u32 bsz = asz;
            if (adsz <= asz)
                bsz = adsz;

            DecryptBuffer(buf + pos, bsz, adsz, secN + i, flags, cryptoInfo);

            pos += bsz;
            adsz -= bsz;
            i++;
        }
    }
}



// From volumes.c + modifies

#define HEADER_OFFSET_CRC                       64
#define HEADER_OFFSET_MAGIC				        68
#define HEADER_OFFSET_DATA                      80
#define HEADER_DATA_SIZE                        0x100
#define HEADER_OFFSET_DATASZ                    74


uint16 GetHeaderField16 (byte *header, size_t offset)
{
    /* modify BE->LE */
	return LE16 (*(uint16 *) (header + offset));
}


uint32 GetHeaderField32 (byte *header, size_t offset)
{
    /* modify BE->LE */
	return LE32 (*(uint32 *) (header + offset));
}


UINT64_STRUCT GetHeaderField64 (byte *header, size_t offset)
{
    /* modify BE->LE */
	UINT64_STRUCT uint64Struct;

	uint64Struct.Value = LE64 (*(uint64 *) (header + offset));
	return uint64Struct;
}


int ReadHeader (int bBoot, char *encryptedHeader, Password *password, PCRYPTO_INFO *retInfo, CRYPTO_INFO *retHeaderCryptoInfo)
{
	char header[HEADER_SIZE];
	KEY_INFO keyInfo;
	PCRYPTO_INFO cryptoInfo;
	char dk[MASTER_KEYDATA_SIZE];
	int pkcs5_prf;
	int status;
	int primaryKeyOffset;


	if (retHeaderCryptoInfo != NULL)
	{
		cryptoInfo = retHeaderCryptoInfo;
	}
	else
	{
		cryptoInfo = *retInfo = crypto_open ();
		if (cryptoInfo == NULL)
			return ERR_OUTOFMEMORY;
	}

	crypto_loadkey (&keyInfo, (char *) password->Text, (int) password->Length);

	// PKCS5 is used to derive the primary header key(s) and secondary header key(s) (XTS mode) from the password
	memcpy (keyInfo.salt, encryptedHeader + HEADER_SALT_OFFSET, PKCS5_SALT_SIZE);

	memset(dk, 0, sizeof(dk));

	// Use this legacy incorrect(for XTS) size, because Daemon Tools use it in this way
	// seems DTools manual upgrade their pre-TrueCrypt5.0 sources
	int keysize = EAGetLargestKey() + LEGACY_VOL_IV_SIZE;

	// Test only rp160/sha1/whirlpool only
	for (pkcs5_prf = FIRST_PRF_ID; pkcs5_prf <= WHIRLPOOL; pkcs5_prf++)
	{
		int lrw64InitDone = 0;		// Deprecated/legacy
		int lrw128InitDone = 0;	// Deprecated/legacy

		keyInfo.noIterations = get_pkcs5_iteration_count (pkcs5_prf, bBoot);

		switch (pkcs5_prf)
		{
		case RIPEMD160:
			derive_key_ripemd160 ((char *) keyInfo.userKey, keyInfo.keyLength, (char *) keyInfo.salt,
				PKCS5_SALT_SIZE, keyInfo.noIterations, dk, keysize);
			break;

		case SHA1:
			// Deprecated/legacy
			derive_key_sha1 ((char *) keyInfo.userKey, keyInfo.keyLength, (char *) keyInfo.salt,
				PKCS5_SALT_SIZE, keyInfo.noIterations, dk, keysize);
			break;

		case WHIRLPOOL:
			derive_key_whirlpool ((char *) keyInfo.userKey, keyInfo.keyLength, (char *) keyInfo.salt,
				PKCS5_SALT_SIZE, keyInfo.noIterations, dk, keysize);
			break;

		default:
			// Unknown/wrong ID
			fatal("ReadHeader(): Unknown/wrong ID\n");
		}

		// Test all available modes of operation
		for (cryptoInfo->mode = FIRST_MODE_OF_OPERATION_ID;
			cryptoInfo->mode <= LAST_MODE_OF_OPERATION;
			cryptoInfo->mode++)
		{
			switch (cryptoInfo->mode)
			{
			case LRW:
			case CBC:
			case INNER_CBC:
			case OUTER_CBC:

				// For LRW (deprecated/legacy), copy the tweak key
				// For CBC (deprecated/legacy), copy the IV/whitening seed
				memcpy (cryptoInfo->k2, dk, LEGACY_VOL_IV_SIZE);
				primaryKeyOffset = LEGACY_VOL_IV_SIZE;
				break;

			default:
				primaryKeyOffset = 0;
			}

			// Test all available encryption algorithms
			for (cryptoInfo->ea = EAGetFirst ();
				cryptoInfo->ea != 0;
				cryptoInfo->ea = EAGetNext (cryptoInfo->ea))
			{
				int blockSize;

				if (!EAIsModeSupported (cryptoInfo->ea, cryptoInfo->mode))
					continue;	// This encryption algorithm has never been available with this mode of operation

				blockSize = CipherGetBlockSize (EAGetFirstCipher (cryptoInfo->ea));

				status = EAInit (cryptoInfo->ea, (unsigned char *) (dk + primaryKeyOffset), cryptoInfo->ks);
				if (status == ERR_CIPHER_INIT_FAILURE)
					goto err;

				// Init objects related to the mode of operation

				if (cryptoInfo->mode == XTS)
				{
					// Copy the secondary key (if cascade, multiple concatenated)
					memcpy (cryptoInfo->k2, dk + EAGetKeySize (cryptoInfo->ea), EAGetKeySize (cryptoInfo->ea));

					// Secondary key schedule
					if (!EAInitMode (cryptoInfo))
					{
						status = ERR_MODE_INIT_FAILED;
						goto err;
					}
				}
				else if (cryptoInfo->mode == LRW
					&& ((blockSize == 8 && !lrw64InitDone) || (blockSize == 16 && !lrw128InitDone)))
				{
					// Deprecated/legacy

					if (!EAInitMode (cryptoInfo))
					{
						status = ERR_MODE_INIT_FAILED;
						goto err;
					}

					if (blockSize == 8)
						lrw64InitDone = 1;
					else if (blockSize == 16)
						lrw128InitDone = 1;
				}

				// Copy the header for decryption
				memcpy (header, encryptedHeader, HEADER_SIZE);

				// Try to decrypt header
				DecryptBlock((unsigned char *) (header + HEADER_ENCRYPTED_DATA_OFFSET), HEADER_ENCRYPTED_DATA_SIZE, HEADER_SIZE, 0, 4, cryptoInfo);

				// Magic 'TRUE'
				if (GetHeaderField32 ((unsigned char *) header, HEADER_OFFSET_MAGIC) != 0x54525545)
					continue;

                uint32_t crc = GetHeaderField32 ((unsigned char *) header, HEADER_OFFSET_CRC);
                if (crc != GetCrc32 ((unsigned char *) ((unsigned char *) (header + HEADER_OFFSET_DATA)), HEADER_DATA_SIZE) )
                    continue;

                if ( GetHeaderField16((unsigned char *) header, HEADER_OFFSET_DATASZ) > 0x100 )
                    continue;


                memcpy(dk, header + HEADER_OFFSET_DATA, HEADER_DATA_SIZE);

                memcpy(encryptedHeader, header, 0x200);

                switch (cryptoInfo->mode)
                {
                case LRW:
                case CBC:
                case INNER_CBC:
                case OUTER_CBC:

                    // For LRW (deprecated/legacy), copy the tweak key
                    // For CBC (deprecated/legacy), copy the IV/whitening seed
                    memcpy (cryptoInfo->k2, dk, LEGACY_VOL_IV_SIZE);
                    primaryKeyOffset = LEGACY_VOL_IV_SIZE;
                    break;

                default:
                    primaryKeyOffset = 0;
                }

                if (EAInit (cryptoInfo->ea, (unsigned char *) (dk + primaryKeyOffset), cryptoInfo->ks) != 0 )
                {
                    status = ERR_MODE_INIT_FAILED;
                    goto err;
                }

                if (cryptoInfo->mode == XTS)
                    memcpy (cryptoInfo->k2, dk + EAGetKeySize (cryptoInfo->ea), EAGetKeySize (cryptoInfo->ea));

				if (!EAInitMode (cryptoInfo))
				{
					status = ERR_MODE_INIT_FAILED;
					goto err;
				}

				// Clear out the temporary key buffers
// ret:
				memset (dk, 0x00, sizeof(dk));
				memset (&keyInfo, 0x00, sizeof (keyInfo));

				return 0;
			}
		}
	}
	status = ERR_PASSWORD_WRONG;

err:
	if (cryptoInfo != retHeaderCryptoInfo)
	{
		crypto_close(cryptoInfo);
		*retInfo = NULL;
	}

	memset (&keyInfo, 0x00, sizeof (keyInfo));
	memset (dk, 0x00, sizeof(dk));
	return status;
}



int decode1(u8 *data, const char *pass, PCRYPTO_INFO *ci)
{
    u32 passlen = 0;
    u8 unsh[0x101];
    memset(unsh, 0, 0x101);
    if (!pass)
    {
        memcpy(unsh, data, 0x40);
        unshuffle1(unsh);
        passlen = 0x40;
    }
    else
    {
        passlen = strlen(pass);
        if (passlen > 0x40)
            passlen = 0x40;
        memcpy(unsh, pass, passlen);
    }

    Password pwd;
    pwd.Length = passlen;
    memcpy(pwd.Text, unsh, passlen);

    return ReadHeader(0, (char *) data, &pwd, ci, NULL);
}



void decryptMode2(Decoder *ctx, u8 *buffer, u32 length, u64 blockIndex)
{
    u8 *p = buffer;
	u8 i[8];
	u8 t[16];
	u64 b;

	*(u64 *)i = BE64(blockIndex);

	for (b = 0; b < length >> 4; b++)
	{
		Gf128MulBy64Tab (i, t, &ctx->gf_ctx);
		Xor128 ((u64 *)p, (u64 *)t);

		aes_decrypt (p, p, &ctx->decr);

		Xor128 ((u64 *)p, (u64 *)t);

		p += 16;

		if (i[7] != 0xff)
			i[7]++;
		else
			*(u64 *)i = BE64 ( BE64(*(u64 *)i) + 1 );
	}
}


void MdxDecryptBufferCBC (Decoder *ctx, u32 *data, unsigned int len, u32 *iv, u32 *whitening)
{
	u32 bufIV[4];
	u64 i;
	u32 ct[4];

	//  IV
	bufIV[0] = iv[0];
	bufIV[1] = iv[1];
    bufIV[2] = iv[2];
    bufIV[3] = iv[3];

	// Decrypt each block
	for (i = 0; i < len/16; i++)
	{
		// Dewhitening
		if (whitening)
		{
			data[0] ^= whitening[0];
			data[1] ^= whitening[1];
            data[2] ^= whitening[0];
            data[3] ^= whitening[1];

            //CBC
            ct[0] = data[0];
            ct[1] = data[1];
            ct[2] = data[2];
            ct[3] = data[3];
		}

        aes_decrypt((u8 *)data, (u8 *)data, &ctx->decr);

		// CBC
		data[0] ^= bufIV[0];
		data[1] ^= bufIV[1];
        data[2] ^= bufIV[2];
        data[3] ^= bufIV[3];

		if (whitening)
		{
			bufIV[0] = ct[0];
			bufIV[1] = ct[1];
            bufIV[2] = ct[2];
            bufIV[3] = ct[3];
		}

		data += 4;
	}
}

void decryptMdxData(Decoder *ctx, u8 *buffer, u32 length, u64 blockSize, u64 blockIndex)
{
    if (ctx->mode == 1)
    {
        if (ctx->ctr)
        {
            u32 sectorIV[4];
            u32 secWhitening[4];
            InitSectorIVAndWhitening (blockIndex, 16, sectorIV, (u64 *)(ctx->dg + 16), secWhitening);
            MdxDecryptBufferCBC (ctx, (u32 *)buffer, length, sectorIV, secWhitening);
        }
        else
        {
            MdxDecryptBufferCBC (ctx, (u32 *)buffer, length, (u32 *)ctx->dg, (u32 *)(ctx->dg + 8));
        }
    }
    else if (ctx->mode == 2)
    {
        if (ctx->ctr)
            decryptMode2(ctx, buffer, length, 1 + (blockSize / 16) * blockIndex);
        else
            decryptMode2(ctx, buffer, length, 1);
    }
    else
    {
        MdxDecryptBufferCBC (ctx, (u32 *)buffer, length, (u32 *)ctx->dg, NULL);
    }
}
