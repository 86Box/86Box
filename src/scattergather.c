/* Copyright holders: SA1988
   see COPYING for more details
*/
/*Scatter/Gather emulation*/
#include <stdlib.h>
#include <string.h>
#include "ibm.h"

#include "scattergather.h"

static uint8_t *SegmentBufferGet(SGBUF *SegmentBuf, uint32_t Data)
{
	uint32_t DataSize;
	uint8_t *Buffer;
	
	if (SegmentBuf->SegmentIndex == SegmentBuf->SegmentNum
				&& !SegmentBuf->SegmentLeft)
	{
		return NULL;
	}
	
	DataSize = MIN(Data, SegmentBuf->SegmentLeft);
	Buffer = SegmentBuf->SegmentPtrCur;
	SegmentBuf->SegmentLeft -= Data;
	
	if (!SegmentBuf->SegmentLeft)
	{
		SegmentBuf->SegmentIndex++;
		
		if (SegmentBuf->SegmentIndex < SegmentBuf->SegmentNum)
		{
			SegmentBuf->SegmentPtrCur = SegmentBuf->SegmentPtr[SegmentBuf->SegmentIndex].Address;
			SegmentBuf->SegmentLeft = SegmentBuf->SegmentPtr[SegmentBuf->SegmentIndex].Length;			
		}
	}
	else
		SegmentBuf->SegmentPtrCur = (uint8_t *)SegmentBuf->SegmentPtrCur + DataSize;
	
	return Buffer;
}

uint8_t *SegmentBufferGetNextSegment(SGBUF *SegmentBuf, uint32_t Segment)
{
	if (!Segment)
		Segment = SegmentBuf->SegmentLeft;
	
	return SegmentBufferGet(SegmentBuf, Segment);
}

uint32_t SegmentBufferCopy(SGBUF *SegmentDst, SGBUF *SegmentSrc, uint32_t Copy)
{
	uint32_t Left = Copy;
	while (Left)
	{
		uint32_t ThisCopy = MIN(MIN(SegmentDst->SegmentLeft, Left), SegmentSrc->SegmentLeft);
		if (!ThisCopy)
			break;
		
		uint32_t Tmp = ThisCopy;
		uint8_t *BufDst = SegmentBufferGet(SegmentDst, Tmp);
		uint8_t *BufSrc = SegmentBufferGet(SegmentSrc, Tmp);

		memcpy(BufSrc, BufDst, ThisCopy);
		
		BufDst += ThisCopy;
		BufSrc -= ThisCopy;
		Left -= ThisCopy;
	}
	
	return Copy - Left;
}

uint32_t SegmentBufferCopyFromBuf(SGBUF *SegmentBuf, uint8_t *BufSrc, uint32_t Copy)
{
	uint32_t Left = Copy;
	while (Left)
	{
		uint32_t ThisCopy = MIN(MIN(SegmentBuf->SegmentLeft, Left), (uint32_t)BufSrc);
		if (!ThisCopy)
			break;
		
		uint32_t Tmp = ThisCopy;
		uint8_t *BufDst = SegmentBufferGet(SegmentBuf, Tmp);

		memcpy(BufDst, BufSrc, ThisCopy);
		
		BufDst += ThisCopy;
		BufSrc -= ThisCopy;
		Left -= ThisCopy;
	}
	
	return Copy - Left;
}

uint32_t SegmentBufferCopyToBuf(SGBUF *SegmentBuf, uint8_t *BufDst, uint32_t Copy)
{
	uint32_t Left = Copy;
	while (Left)
	{
		uint32_t ThisCopy = MIN(MIN(SegmentBuf->SegmentLeft, Left), (uint32_t)BufDst);
		if (!ThisCopy)
			break;
		
		uint32_t Tmp = ThisCopy;
		uint8_t *BufSrc = SegmentBufferGet(SegmentBuf, Tmp);

		memcpy(BufSrc, BufDst, ThisCopy);
		
		BufSrc += ThisCopy;
		BufDst -= ThisCopy;
		Left -= ThisCopy;
	}
	
	return Copy - Left;
}

uint32_t SegmentBufferAdvance(SGBUF *SegmentBuf, uint32_t Advance)
{
	uint32_t Left = Advance;
	while (Left)
	{
		uint32_t ThisAdvance = Left;
		SegmentBufferGet(SegmentBuf, ThisAdvance);
		if (!ThisAdvance)
			break;
		
		Left -= ThisAdvance;
	}
	
	return Advance - Left;
}

void SegmentBufferInit(SGBUF *SegmentBuf, const SGSEG *SegmentPtr, uint32_t Segments)
{
	SegmentBuf->SegmentPtr = SegmentPtr;
	SegmentBuf->SegmentNum = (unsigned)Segments;
	SegmentBuf->SegmentIndex = 0;
	
	if (Segments && SegmentPtr)
	{
		SegmentBuf->SegmentPtrCur = SegmentPtr[0].Address;
		SegmentBuf->SegmentLeft = SegmentPtr[0].Length;
	}
	else
	{
		SegmentBuf->SegmentPtrCur = NULL;
		SegmentBuf->SegmentLeft = 0;		
	}
}