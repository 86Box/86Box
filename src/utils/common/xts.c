/*
 Copyright (c) 2008 TrueCrypt Foundation. All rights reserved.

 Governed by the TrueCrypt License 2.4 the full text of which is contained
 in the file License.txt included in TrueCrypt binary and source code
 distribution packages.
*/

/* For low-memory environments, define XTS_LOW_RESOURCE_VERSION, which will save
0.5 KB of RAM, but the speed will be 15-20% lower. However, on multi-core CPUs,
the XTS_LOW_RESOURCE_VERSION code might eventually be faster when parallelized,
because it processes the buffer continuously as a whole -- it does not divide the
buffer into data units (nevertheless, note that GenerateWhiteningValues supports
more than one data unit).

Note that when TC_NO_COMPILER_INT64 is defined, XTS_LOW_RESOURCE_VERSION is implicitly
defined as well (because the non-low-resource version needs 64-bit types).

For big-endian platforms (PowerPC, SPARC, etc.) define BYTE_ORDER as BIG_ENDIAN. */


#ifdef TC_MINIMIZE_CODE_SIZE
#	define XTS_LOW_RESOURCE_VERSION
#	pragma optimize ("tl", on)
#endif

#ifdef TC_NO_COMPILER_INT64
#	ifndef XTS_LOW_RESOURCE_VERSION
#		define XTS_LOW_RESOURCE_VERSION
#	endif
#endif


#include "xts.h"


#ifndef XTS_LOW_RESOURCE_VERSION

// length: number of bytes to encrypt; may be larger than one data unit and must be divisible by the cipher block size
// ks: the primary key schedule
// ks2: the secondary key schedule
// startDataUnitNo: The sequential number of the data unit with which the buffer starts.
// startCipherBlockNo: The sequential number of the first plaintext block to encrypt inside the data unit startDataUnitNo.
//                     When encrypting the data unit from its first block, startCipherBlockNo is 0.
//                     The startCipherBlockNo value applies only to the first data unit in the buffer; each successive
//                     data unit is encrypted from its first block. The start of the buffer does not have to be
//                     aligned with the start of a data unit. If it is aligned, startCipherBlockNo must be 0; if it
//                     is not aligned, startCipherBlockNo must reflect the misalignment accordingly.
void EncryptBufferXTS (uint8_t *buffer,
					   TC_LARGEST_COMPILER_UINT length,
					   const UINT64_STRUCT *startDataUnitNo,
					   unsigned int startCipherBlockNo,
					   uint8_t *ks,
					   uint8_t *ks2,
					   int cipher)
{
	uint8_t finalCarry;
	uint8_t whiteningValues [ENCRYPTION_DATA_UNIT_SIZE];
	uint8_t whiteningValue [BYTES_PER_XTS_BLOCK];
	uint8_t byteBufUnitNo [BYTES_PER_XTS_BLOCK];
	uint64_t *whiteningValuesPtr64 = (uint64_t *) whiteningValues;
	uint64_t *whiteningValuePtr64 = (uint64_t *) whiteningValue;
	uint64_t *bufPtr = (uint64_t *) buffer;
	unsigned int startBlock = startCipherBlockNo, endBlock, block;
	uint64_t *const finalInt64WhiteningValuesPtr = whiteningValuesPtr64 + sizeof (whiteningValues) / sizeof (*whiteningValuesPtr64) - 1;
	TC_LARGEST_COMPILER_UINT blockCount, dataUnitNo;

	/* The encrypted data unit number (i.e. the resultant ciphertext block) is to be multiplied in the
	finite field GF(2^128) by j-th power of n, where j is the sequential plaintext/ciphertext block
	number and n is 2, a primitive element of GF(2^128). This can be (and is) simplified and implemented
	as a left shift of the preceding whitening value by one bit (with carry propagating). In addition, if
	the shift of the highest byte results in a carry, 135 is XORed into the lowest byte. The value 135 is
	derived from the modulus of the Galois Field (x^128+x^7+x^2+x+1). */

	// Convert the 64-bit data unit number into a little-endian 16-byte array.
	// Note that as we are converting a 64-bit number into a 16-byte array we can always zero the last 8 bytes.
	dataUnitNo = startDataUnitNo->Value;
	*((uint64_t *) byteBufUnitNo) = LE64 (dataUnitNo);
	*((uint64_t *) byteBufUnitNo + 1) = 0;

	if (length % BYTES_PER_XTS_BLOCK)
		TC_THROW_FATAL_EXCEPTION;

	blockCount = length / BYTES_PER_XTS_BLOCK;

	// Process all blocks in the buffer
	// When length > ENCRYPTION_DATA_UNIT_SIZE, this can be parallelized (one data unit per core)
	while (blockCount > 0)
	{
		if (blockCount < BLOCKS_PER_XTS_DATA_UNIT)
			endBlock = startBlock + (unsigned int) blockCount;
		else
			endBlock = BLOCKS_PER_XTS_DATA_UNIT;

		whiteningValuesPtr64 = finalInt64WhiteningValuesPtr;
		whiteningValuePtr64 = (uint64_t *) whiteningValue;

		// Encrypt the data unit number using the secondary key (in order to generate the first
		// whitening value for this data unit)
		*whiteningValuePtr64 = *((uint64_t *) byteBufUnitNo);
		*(whiteningValuePtr64 + 1) = 0;
		EncipherBlock (cipher, whiteningValue, ks2);

		// Generate subsequent whitening values for blocks in this data unit. Note that all generated 128-bit
		// whitening values are stored in memory as a sequence of 64-bit integers in reverse order.
		for (block = 0; block < endBlock; block++)
		{
			if (block >= startBlock)
			{
				*whiteningValuesPtr64-- = *whiteningValuePtr64++;
				*whiteningValuesPtr64-- = *whiteningValuePtr64;
			}
			else
				whiteningValuePtr64++;

			// Derive the next whitening value

#if BYTE_ORDER == LITTLE_ENDIAN

			// Little-endian platforms (Intel, AMD, etc.)

			finalCarry =
				(*whiteningValuePtr64 & 0x8000000000000000) ?
				135 : 0;

			*whiteningValuePtr64-- <<= 1;

			if (*whiteningValuePtr64 & 0x8000000000000000)
				*(whiteningValuePtr64 + 1) |= 1;

			*whiteningValuePtr64 <<= 1;

#else
			// Big-endian platforms (PowerPC, Motorola, etc.)

			finalCarry =
				(*whiteningValuePtr64 & 0x80) ?
				135 : 0;

			*whiteningValuePtr64 = LE64 (LE64 (*whiteningValuePtr64) << 1);

			whiteningValuePtr64--;

			if (*whiteningValuePtr64 & 0x80)
				*(whiteningValuePtr64 + 1) |= 0x0100000000000000;

			*whiteningValuePtr64 = LE64 (LE64 (*whiteningValuePtr64) << 1);
#endif

			whiteningValue[0] ^= finalCarry;
		}

		whiteningValuesPtr64 = finalInt64WhiteningValuesPtr;

		// Encrypt all blocks in this data unit
		// TO DO: This should be parallelized (one block per core)
		for (block = startBlock; block < endBlock; block++)
		{
			// Pre-whitening
			*bufPtr++ ^= *whiteningValuesPtr64--;
			*bufPtr-- ^= *whiteningValuesPtr64++;

			// Actual encryption
			EncipherBlock (cipher, bufPtr, ks);

			// Post-whitening
			*bufPtr++ ^= *whiteningValuesPtr64--;
			*bufPtr++ ^= *whiteningValuesPtr64--;

			blockCount--;
		}

		startBlock = 0;

		dataUnitNo++;

		*((uint64_t *) byteBufUnitNo) = LE64 (dataUnitNo);
	}

	FAST_ERASE64 (whiteningValue, sizeof(whiteningValue));
	FAST_ERASE64 (whiteningValues, sizeof(whiteningValues));
}


// For descriptions of the input parameters, see EncryptBufferXTS().
void DecryptBufferXTS (uint8_t *buffer,
					   TC_LARGEST_COMPILER_UINT length,
					   const UINT64_STRUCT *startDataUnitNo,
					   unsigned int startCipherBlockNo,
					   uint8_t *ks,
					   uint8_t *ks2,
					   int cipher)
{
	uint8_t finalCarry;
	uint8_t whiteningValues [ENCRYPTION_DATA_UNIT_SIZE];
	uint8_t whiteningValue [BYTES_PER_XTS_BLOCK];
	uint8_t byteBufUnitNo [BYTES_PER_XTS_BLOCK];
	uint64_t *whiteningValuesPtr64 = (uint64_t *) whiteningValues;
	uint64_t *whiteningValuePtr64 = (uint64_t *) whiteningValue;
	uint64_t *bufPtr = (uint64_t *) buffer;
	unsigned int startBlock = startCipherBlockNo, endBlock, block;
	uint64_t *const finalInt64WhiteningValuesPtr = whiteningValuesPtr64 + sizeof (whiteningValues) / sizeof (*whiteningValuesPtr64) - 1;
	TC_LARGEST_COMPILER_UINT blockCount, dataUnitNo;

	// Convert the 64-bit data unit number into a little-endian 16-byte array.
	// Note that as we are converting a 64-bit number into a 16-byte array we can always zero the last 8 bytes.
	dataUnitNo = startDataUnitNo->Value;
	*((uint64_t *) byteBufUnitNo) = LE64 (dataUnitNo);
	*((uint64_t *) byteBufUnitNo + 1) = 0;

	if (length % BYTES_PER_XTS_BLOCK)
		TC_THROW_FATAL_EXCEPTION;

	blockCount = length / BYTES_PER_XTS_BLOCK;

	// Process all blocks in the buffer
	// When length > ENCRYPTION_DATA_UNIT_SIZE, this can be parallelized (one data unit per core)
	while (blockCount > 0)
	{
		if (blockCount < BLOCKS_PER_XTS_DATA_UNIT)
			endBlock = startBlock + (unsigned int) blockCount;
		else
			endBlock = BLOCKS_PER_XTS_DATA_UNIT;

		whiteningValuesPtr64 = finalInt64WhiteningValuesPtr;
		whiteningValuePtr64 = (uint64_t *) whiteningValue;

		// Encrypt the data unit number using the secondary key (in order to generate the first
		// whitening value for this data unit)
		*whiteningValuePtr64 = *((uint64_t *) byteBufUnitNo);
		*(whiteningValuePtr64 + 1) = 0;
		EncipherBlock (cipher, whiteningValue, ks2);

		// Generate subsequent whitening values for blocks in this data unit. Note that all generated 128-bit
		// whitening values are stored in memory as a sequence of 64-bit integers in reverse order.
		for (block = 0; block < endBlock; block++)
		{
			if (block >= startBlock)
			{
				*whiteningValuesPtr64-- = *whiteningValuePtr64++;
				*whiteningValuesPtr64-- = *whiteningValuePtr64;
			}
			else
				whiteningValuePtr64++;

			// Derive the next whitening value

#if BYTE_ORDER == LITTLE_ENDIAN

			// Little-endian platforms (Intel, AMD, etc.)

			finalCarry =
				(*whiteningValuePtr64 & 0x8000000000000000) ?
				135 : 0;

			*whiteningValuePtr64-- <<= 1;

			if (*whiteningValuePtr64 & 0x8000000000000000)
				*(whiteningValuePtr64 + 1) |= 1;

			*whiteningValuePtr64 <<= 1;

#else
			// Big-endian platforms (PowerPC, Motorola, etc.)

			finalCarry =
				(*whiteningValuePtr64 & 0x80) ?
				135 : 0;

			*whiteningValuePtr64 = LE64 (LE64 (*whiteningValuePtr64) << 1);

			whiteningValuePtr64--;

			if (*whiteningValuePtr64 & 0x80)
				*(whiteningValuePtr64 + 1) |= 0x0100000000000000;

			*whiteningValuePtr64 = LE64 (LE64 (*whiteningValuePtr64) << 1);
#endif

			whiteningValue[0] ^= finalCarry;
		}

		whiteningValuesPtr64 = finalInt64WhiteningValuesPtr;

		// Decrypt blocks in this data unit
		// TO DO: This should be parallelized (one block per core)
		for (block = startBlock; block < endBlock; block++)
		{
			*bufPtr++ ^= *whiteningValuesPtr64--;
			*bufPtr-- ^= *whiteningValuesPtr64++;

			DecipherBlock (cipher, bufPtr, ks);

			*bufPtr++ ^= *whiteningValuesPtr64--;
			*bufPtr++ ^= *whiteningValuesPtr64--;

			blockCount--;
		}

		startBlock = 0;

		dataUnitNo++;

		*((uint64_t *) byteBufUnitNo) = LE64 (dataUnitNo);
	}

	FAST_ERASE64 (whiteningValue, sizeof(whiteningValue));
	FAST_ERASE64 (whiteningValues, sizeof(whiteningValues));
}


#if 0	// The following function is currently unused but may be useful in future

// Generates XTS whitening values. Use this function if you need to generate whitening values for more than
// one data unit in one pass (the value 'length' may be greater than the data unit size). 'buffer' must point
// to the LAST 8 bytes of the buffer for the whitening values. Note that the generated 128-bit whitening values
// are stored in memory as a sequence of 64-bit integers in reverse order. For descriptions of the input
// parameters, see EncryptBufferXTS().
static void GenerateWhiteningValues (uint64_t *bufPtr64,
							TC_LARGEST_COMPILER_UINT length,
							const UINT64_STRUCT *startDataUnitNo,
							unsigned int startBlock,
							uint8_t *ks2,
							int cipher)
{
	unsigned int block;
	unsigned int endBlock;
	uint8_t byteBufUnitNo [BYTES_PER_XTS_BLOCK];
	uint8_t whiteningValue [BYTES_PER_XTS_BLOCK];
	uint64_t *whiteningValuePtr64 = (uint64_t *) whiteningValue;
	uint8_t finalCarry;
	uint64_t *const finalInt64WhiteningValuePtr = whiteningValuePtr64 + sizeof (whiteningValue) / sizeof (*whiteningValuePtr64) - 1;
	TC_LARGEST_COMPILER_UINT blockCount, dataUnitNo;

	dataUnitNo = startDataUnitNo->Value;

	blockCount = length / BYTES_PER_XTS_BLOCK;

	// Convert the 64-bit data unit number into a little-endian 16-byte array.
	// Note that as we are converting a 64-bit number into a 16-byte array we can always zero the last 8 bytes.
	*((uint64_t *) byteBufUnitNo) = LE64 (dataUnitNo);
	*((uint64_t *) byteBufUnitNo + 1) = 0;

	// Generate the whitening values.
	// When length > ENCRYPTION_DATA_UNIT_SIZE, this can be parallelized (one data unit per core)
	while (blockCount > 0)
	{
		if (blockCount < BLOCKS_PER_XTS_DATA_UNIT)
			endBlock = startBlock + (unsigned int) blockCount;
		else
			endBlock = BLOCKS_PER_XTS_DATA_UNIT;

		// Encrypt the data unit number using the secondary key (in order to generate the first
		// whitening value for this data unit)
		memcpy (whiteningValue, byteBufUnitNo, BYTES_PER_XTS_BLOCK);
		EncipherBlock (cipher, whiteningValue, ks2);

		// Process all blocks in this data unit
		for (block = 0; block < endBlock; block++)
		{
			if (block >= startBlock)
			{
				whiteningValuePtr64 = (uint64_t *) whiteningValue;

				*bufPtr64-- = *whiteningValuePtr64++;
				*bufPtr64-- = *whiteningValuePtr64;

				blockCount--;
			}

			// Derive the next whitening value

			whiteningValuePtr64 = finalInt64WhiteningValuePtr;

#if BYTE_ORDER == LITTLE_ENDIAN

			// Little-endian platforms (Intel, AMD, etc.)

			finalCarry =
				(*whiteningValuePtr64 & 0x8000000000000000) ?
				135 : 0;

			*whiteningValuePtr64-- <<= 1;

			if (*whiteningValuePtr64 & 0x8000000000000000)
				*(whiteningValuePtr64 + 1) |= 1;

			*whiteningValuePtr64 <<= 1;

#else
			// Big-endian platforms (PowerPC, Motorola, etc.)

			finalCarry =
				(*whiteningValuePtr64 & 0x80) ?
				135 : 0;

			*whiteningValuePtr64 = LE64 (LE64 (*whiteningValuePtr64) << 1);

			whiteningValuePtr64--;

			if (*whiteningValuePtr64 & 0x80)
				*(whiteningValuePtr64 + 1) |= 0x0100000000000000;

			*whiteningValuePtr64 = LE64 (LE64 (*whiteningValuePtr64) << 1);
#endif

			whiteningValue[0] ^= finalCarry;
		}

		startBlock = 0;

		dataUnitNo++;

		// Convert the 64-bit data unit number into a little-endian 16-byte array.
		*((uint64_t *) byteBufUnitNo) = LE64 (dataUnitNo);
	}

	FAST_ERASE64 (whiteningValue, sizeof(whiteningValue));
}
#endif	// #if 0


#else	// XTS_LOW_RESOURCE_VERSION


#if BYTE_ORDER == BIG_ENDIAN
#error XTS_LOW_RESOURCE_VERSION is not compatible with big-endian platforms
#endif


// Increases a 64-bit value by one in a way compatible with non-64-bit environments/platforms
static void IncUint64Struct (UINT64_STRUCT *uint64Struct)
{
#ifdef TC_NO_COMPILER_INT64
	if (!++uint64Struct->LowPart)
	{
		uint64Struct->HighPart++;
	}
#else
	uint64Struct->Value++;
#endif
}


// Converts a 64-bit unsigned integer (passed as two 32-bit integers for compatibility with non-64-bit
// environments/platforms) into a little-endian 16-byte array.
static void Uint64ToLE16ByteArray (uint8_t *byteBuf, unsigned __int32 highInt32, unsigned __int32 lowInt32)
{
	unsigned __int32 *bufPtr32 = (unsigned __int32 *) byteBuf;

	*bufPtr32++ = lowInt32;
	*bufPtr32++ = highInt32;

	// We're converting a 64-bit number into a little-endian 16-byte array so we can zero the last 8 bytes
	*bufPtr32++ = 0;
	*bufPtr32 = 0;
}


// Generates and XORs XTS whitening values into blocks in the buffer.
// For descriptions of the input parameters, see EncryptBufferXTS().
static void WhiteningPass (uint8_t *buffer,
							TC_LARGEST_COMPILER_UINT length,
							const UINT64_STRUCT *startDataUnitNo,
							unsigned int startBlock,
							uint8_t *ks2,
							int cipher)
{
	TC_LARGEST_COMPILER_UINT blockCount;
	UINT64_STRUCT dataUnitNo;
	unsigned int block;
	unsigned int endBlock;
	uint8_t byteBufUnitNo [BYTES_PER_XTS_BLOCK];
	uint8_t whiteningValue [BYTES_PER_XTS_BLOCK];
	unsigned __int32 *bufPtr32 = (unsigned __int32 *) buffer;
	unsigned __int32 *whiteningValuePtr32 = (unsigned __int32 *) whiteningValue;
	uint8_t finalCarry;
	unsigned __int32 *const finalDwordWhiteningValuePtr = whiteningValuePtr32 + sizeof (whiteningValue) / sizeof (*whiteningValuePtr32) - 1;

	// Store the 64-bit data unit number in a way compatible with non-64-bit environments/platforms
	dataUnitNo.HighPart = startDataUnitNo->HighPart;
	dataUnitNo.LowPart = startDataUnitNo->LowPart;

	blockCount = length / BYTES_PER_XTS_BLOCK;

	// Convert the 64-bit data unit number into a little-endian 16-byte array.
	// (Passed as two 32-bit integers for compatibility with non-64-bit environments/platforms.)
	Uint64ToLE16ByteArray (byteBufUnitNo, dataUnitNo.HighPart, dataUnitNo.LowPart);

	// Generate whitening values for all blocks in the buffer
	while (blockCount > 0)
	{
		if (blockCount < BLOCKS_PER_XTS_DATA_UNIT)
			endBlock = startBlock + (unsigned int) blockCount;
		else
			endBlock = BLOCKS_PER_XTS_DATA_UNIT;

		// Encrypt the data unit number using the secondary key (in order to generate the first
		// whitening value for this data unit)
		memcpy (whiteningValue, byteBufUnitNo, BYTES_PER_XTS_BLOCK);
		EncipherBlock (cipher, whiteningValue, ks2);

		// Generate subsequent whitening values and XOR each whitening value into corresponding
		// ciphertext/plaintext block

		for (block = 0; block < endBlock; block++)
		{
			if (block >= startBlock)
			{
				whiteningValuePtr32 = (unsigned __int32 *) whiteningValue;

				// XOR the whitening value into this ciphertext/plaintext block
				*bufPtr32++ ^= *whiteningValuePtr32++;
				*bufPtr32++ ^= *whiteningValuePtr32++;
				*bufPtr32++ ^= *whiteningValuePtr32++;
				*bufPtr32++ ^= *whiteningValuePtr32;

				blockCount--;
			}

			// Derive the next whitening value

			finalCarry = 0;

			for (whiteningValuePtr32 = finalDwordWhiteningValuePtr;
				whiteningValuePtr32 >= (unsigned __int32 *) whiteningValue;
				whiteningValuePtr32--)
			{
				if (*whiteningValuePtr32 & 0x80000000)	// If the following shift results in a carry
				{
					if (whiteningValuePtr32 != finalDwordWhiteningValuePtr)	// If not processing the highest double word
					{
						// A regular carry
						*(whiteningValuePtr32 + 1) |= 1;
					}
					else
					{
						// The highest byte shift will result in a carry
						finalCarry = 135;
					}
				}

				*whiteningValuePtr32 <<= 1;
			}

			whiteningValue[0] ^= finalCarry;
		}

		startBlock = 0;

		// Increase the data unit number by one
		IncUint64Struct (&dataUnitNo);

		// Convert the 64-bit data unit number into a little-endian 16-byte array.
		Uint64ToLE16ByteArray (byteBufUnitNo, dataUnitNo.HighPart, dataUnitNo.LowPart);
	}

	FAST_ERASE64 (whiteningValue, sizeof(whiteningValue));
}


// length: number of bytes to encrypt; may be larger than one data unit and must be divisible by the cipher block size
// ks: the primary key schedule
// ks2: the secondary key schedule
// dataUnitNo: The sequential number of the data unit with which the buffer starts.
// startCipherBlockNo: The sequential number of the first plaintext block to encrypt inside the data unit dataUnitNo.
//                     When encrypting the data unit from its first block, startCipherBlockNo is 0.
//                     The startCipherBlockNo value applies only to the first data unit in the buffer; each successive
//                     data unit is encrypted from its first block. The start of the buffer does not have to be
//                     aligned with the start of a data unit. If it is aligned, startCipherBlockNo must be 0; if it
//                     is not aligned, startCipherBlockNo must reflect the misalignment accordingly.
void EncryptBufferXTS (uint8_t *buffer,
					   TC_LARGEST_COMPILER_UINT length,
					   const UINT64_STRUCT *dataUnitNo,
					   unsigned int startCipherBlockNo,
					   uint8_t *ks,
					   uint8_t *ks2,
					   int cipher)
{
	TC_LARGEST_COMPILER_UINT blockCount;
	uint8_t *bufPtr = buffer;

	if (length % BYTES_PER_XTS_BLOCK)
		TC_THROW_FATAL_EXCEPTION;

	// Pre-whitening (all plaintext blocks in the buffer)
	WhiteningPass (buffer, length, dataUnitNo, startCipherBlockNo, ks2, cipher);

	// Encrypt all plaintext blocks in the buffer
	for (blockCount = 0; blockCount < length / BYTES_PER_XTS_BLOCK; blockCount++)
	{
		EncipherBlock (cipher, bufPtr, ks);
		bufPtr += BYTES_PER_XTS_BLOCK;
	}

	// Post-whitening (all ciphertext blocks in the buffer)
	WhiteningPass (buffer, length, dataUnitNo, startCipherBlockNo, ks2, cipher);
}


// For descriptions of the input parameters, see EncryptBufferXTS().
void DecryptBufferXTS (uint8_t *buffer,
					   TC_LARGEST_COMPILER_UINT length,
					   const UINT64_STRUCT *dataUnitNo,
					   unsigned int startCipherBlockNo,
					   uint8_t *ks,
					   uint8_t *ks2,
					   int cipher)
{
	TC_LARGEST_COMPILER_UINT blockCount;
	uint8_t *bufPtr = buffer;

	if (length % BYTES_PER_XTS_BLOCK)
		TC_THROW_FATAL_EXCEPTION;

	WhiteningPass (buffer, length, dataUnitNo, startCipherBlockNo, ks2, cipher);

	for (blockCount = 0; blockCount < length / BYTES_PER_XTS_BLOCK; blockCount++)
	{
		DecipherBlock (cipher, bufPtr, ks);
		bufPtr += BYTES_PER_XTS_BLOCK;
	}

	WhiteningPass (buffer, length, dataUnitNo, startCipherBlockNo, ks2, cipher);
}

#endif	// XTS_LOW_RESOURCE_VERSION
