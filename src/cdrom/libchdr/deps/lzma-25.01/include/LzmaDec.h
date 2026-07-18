/* Namespace some symbols to avoid linker errors in static libretro builds. */
#define LzmaDec_InitDicAndState CHDR_LzmaDec_InitDicAndState
#define LzmaDec_Init CHDR_LzmaDec_Init
#define LzmaDec_DecodeToDic CHDR_LzmaDec_DecodeToDic
#define LzmaDec_DecodeToBuf CHDR_LzmaDec_DecodeToBuf
#define LzmaDec_FreeProbs CHDR_LzmaDec_FreeProbs
#define LzmaDec_Free CHDR_LzmaDec_Free
#define LzmaProps_Decode CHDR_LzmaProps_Decode
#define LzmaDec_AllocateProbs CHDR_LzmaDec_AllocateProbs
#define LzmaDec_Allocate CHDR_LzmaDec_Allocate
#define LzmaDecode CHDR_LzmaDecode

#include "real/LzmaDec.h"
