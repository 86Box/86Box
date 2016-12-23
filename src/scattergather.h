#ifndef _SCATTERGATHER_H_
#define _SCATTERGATHER_H_

typedef struct SGSEG
{
	uint8_t Address[512];
	uint32_t Length;
} SGSEG;

typedef struct SGBUF
{
	const SGSEG *SegmentPtr;
	unsigned SegmentNum;
	unsigned SegmentIndex;
	uint8_t *SegmentPtrCur;
	uint32_t SegmentLeft;
} SGBUF;

uint32_t SegmentBufferCopy(SGBUF *SegmentDst, SGBUF *SegmentSrc, uint32_t Copy);
uint8_t *SegmentBufferGetNextSegment(SGBUF *SegmentBuf, uint32_t Segment);
uint32_t SegmentBufferAdvance(SGBUF *SegmentBuf, uint32_t Advance);
void SegmentBufferInit(SGBUF *SegmentBuf, const SGSEG *SegmentPtr, uint32_t Segments);

#endif