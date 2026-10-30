/*
 * Copyright (c) 2020 Bitdefender
 * SPDX-License-Identifier: Apache-2.0
 */
#include "include/bddisasm_crt.h"
#include "../inc/bddisasm.h"

// The table definitions & internal headers.
#include "include/bdx86_tabledefs.h"
#include "include/bdx86_operand.h"

// Include auto-generated stuff.
#include "bdx86_table_root.h"
#include "bdx86_table_xop.h"
#include "bdx86_table_vex.h"
#include "bdx86_table_evex.h"

#include "bdx86_prefixes.h"
#include "bdx86_modrm.h"
#include "bdx86_rex2.h"


#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) ((void)(P))
#endif


static const ND_UINT8 gDispsizemap16[4][8] =
{
    { 0, 0, 0, 0, 0, 0, 2, 0 },
    { 1, 1, 1, 1, 1, 1, 1, 1 },
    { 2, 2, 2, 2, 2, 2, 2, 2 },
    { 0, 0, 0, 0, 0, 0, 0, 0 },
};

static const ND_UINT8 gDispsizemap[4][8] =
{
    { 0, 0, 0, 0, 0, 4, 0, 0 },
    { 1, 1, 1, 1, 1, 1, 1, 1 },
    { 4, 4, 4, 4, 4, 4, 4, 4 },
    { 0, 0, 0, 0, 0, 0, 0, 0 },
};

static const ND_TABLE_EX_M gLegacyMap_mmmmm = 
{
    .Type = ND_ILUT_EX_M,
    .Table = 
    {
        (const ND_TABLE *)&gLegacyMap_opcode,
        (const ND_TABLE *)&gLegacyMap_opcode_0f_opcode,
        (const ND_TABLE *)&gLegacyMap_opcode_0f_opcode_38_opcode,
        (const ND_TABLE *)&gLegacyMap_opcode_0f_opcode_3a_opcode
    }
};


typedef struct ND_INTERNAL_CONTEXT
{
    const ND_CONTEXT  *Context;
    const ND_IDBE     *Idbe;
    INSTRUX_MINI      *Instrux;
} ND_INTERNAL_CONTEXT;


//
// NdGetVersion
//
void
NdGetVersion(
    ND_UINT32 *Major,
    ND_UINT32 *Minor,
    ND_UINT32 *Revision,
    const char **BuildDate,
    const char **BuildTime
)
{
    if (ND_NULL != Major)
    {
        *Major = DISASM_VERSION_MAJOR;
    }

    if (ND_NULL != Minor)
    {
        *Minor = DISASM_VERSION_MINOR;
    }

    if (ND_NULL != Revision)
    {
        *Revision = DISASM_VERSION_REVISION;
    }

//
// Do not use __TIME__ and __DATE__ macros when compiling against a kernel tree.
//
#if defined(__KERNEL__)

    if (ND_NULL != BuildDate)
    {
        *BuildDate = (char *)ND_NULL;
    }

    if (ND_NULL != BuildTime)
    {
        *BuildTime = (char *)ND_NULL;
    }

#else

    if (ND_NULL != BuildDate)
    {
        *BuildDate = __DATE__;
    }

    if (ND_NULL != BuildTime)
    {
        *BuildTime = __TIME__;
    }

#endif

}


//
// NdIncrementLength
//
static inline
NDSTATUS
NdIncrementLength(
    INSTRUX_MINI *Instrux,
    ND_UINT8 Length
)
{
    Instrux->Length += Length;

    if (Instrux->Length > ND_MAX_INSTRUCTION_LENGTH)
    {
        return ND_STATUS_INSTRUCTION_TOO_LONG;
    }

    return ND_STATUS_SUCCESS;
}


//
// NdFetchXop
//
static NDSTATUS
NdFetchXop(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size
)
{
#define Context     Ictx->Context
#define Instrux     Ictx->Instrux

    // Offset points to the 0x8F XOP prefix.
    // One more byte has to follow, the modrm or the second XOP byte.
    RET_GT((ND_SIZET)Offset + 2, Size, ND_STATUS_BUFFER_TOO_SMALL);

    if (((Code[Offset + 1] & 0x1F) < 8))
    {
        return ND_STATUS_SUCCESS;
    }

    // XOP found, make sure the third byte is here.
    RET_GT((ND_SIZET)Offset + 3, Size, ND_STATUS_BUFFER_TOO_SMALL);

    // Make sure we don't have any other prefix.
    if (Instrux->HasOpSize || 
        Instrux->HasRepnzXacquireBnd || 
        Instrux->HasRepRepzXrelease || 
        Instrux->HasRex || 
        Instrux->HasRex2 ||
        Instrux->HasLock)
    {
        return ND_STATUS_XOP_WITH_PREFIX;
    }

    // Fill in XOP info.
    Instrux->HasXop = ND_TRUE;
    Instrux->EncMode = ND_ENCM_XOP;
    Instrux->Xop.Xop[0] = Code[Offset];
    Instrux->Xop.Xop[1] = Code[Offset + 1];
    Instrux->Xop.Xop[2] = Code[Offset + 2];

    Instrux->Exs.w = Instrux->Xop.w;
    Instrux->Exs.r = (ND_UINT32)~Instrux->Xop.r;
    Instrux->Exs.x = (ND_UINT32)~Instrux->Xop.x;
    Instrux->Exs.b = (ND_UINT32)~Instrux->Xop.b;
    Instrux->Exs.l = Instrux->Xop.l;
    Instrux->Exs.v = (ND_UINT32)~Instrux->Xop.v;
    Instrux->Exs.m = Instrux->Xop.m;
    Instrux->Exs.p = Instrux->Xop.p;

    // if we are in non 64 bit mode, we must make sure that none of the extended registers are being addressed.
    if (ND_CODE_64 != Instrux->DefCode)
    {
        // Xop.R and Xop.X must be 1 (inverted).
        if ((Instrux->Exs.r | Instrux->Exs.x) == 1)
        {
            return ND_STATUS_INVALID_ENCODING_IN_MODE;
        }

        // Xop.V must be less than 8.
        if ((Instrux->Exs.v & 0x8) == 0x8)
        {
            return ND_STATUS_INVALID_ENCODING_IN_MODE;
        }

        // Xop.B is ignored, so we force it to 0.
        Instrux->Exs.b = 0;
    }
    else
    {
        // 64-bit mode, promote operand size to 64-bit if W is set.
        if (Instrux->Exs.w == 1)
        {
            Instrux->OpMode = ND_OPSZ_64;
        }
    }

    // Update Instrux length & offset, and make sure we don't exceed 15 bytes.
    return NdIncrementLength(Instrux, 3);

#undef Context
#undef Instrux
}


//
// NdFetchVex2
//
static NDSTATUS
NdFetchVex2(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size
)
{
#define Context     Ictx->Context
#define Instrux     Ictx->Instrux

    // One more byte has to follow, the modrm or the second VEX byte.
    RET_GT((ND_SIZET)Offset + 2, Size, ND_STATUS_BUFFER_TOO_SMALL);

    if ((ND_CODE_64 != Instrux->DefCode) && ((Code[Offset + 1] & 0xC0) != 0xC0))
    {
        return ND_STATUS_SUCCESS;
    }

    // Make sure we don't have any other prefix.
    if (Instrux->HasOpSize || 
        Instrux->HasRepnzXacquireBnd ||
        Instrux->HasRepRepzXrelease || 
        Instrux->HasRex || 
        Instrux->HasRex2 || 
        Instrux->HasLock)
    {
        return ND_STATUS_VEX_WITH_PREFIX;
    }

    // Fill in VEX2 info.
    Instrux->VexMode = ND_VEXM_2B;
    Instrux->HasVex = ND_TRUE;
    Instrux->EncMode = ND_ENCM_VEX;
    Instrux->Vex2.Vex[0] = Code[Offset];
    Instrux->Vex2.Vex[1] = Code[Offset + 1];

    Instrux->Exs.m = 1; // For VEX2 instructions, always use the second table.
    Instrux->Exs.r = (ND_UINT32)~Instrux->Vex2.r;
    Instrux->Exs.v = (ND_UINT32)~Instrux->Vex2.v;
    Instrux->Exs.l = Instrux->Vex2.l;
    Instrux->Exs.p = Instrux->Vex2.p;

    // Apply 64-bit operand size.
    if (Instrux->Exs.w == 1 && Instrux->DefCode == ND_CODE_64)
    {
        Instrux->OpMode = ND_OPSZ_64;
    }

    // Update Instrux length & offset, and make sure we don't exceed 15 bytes.
    return NdIncrementLength(Instrux, 2);

#undef Instrux
#undef Context
}


//
// NdFetchVex3
//
static NDSTATUS
NdFetchVex3(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size
)
{
#define Context     Ictx->Context
#define Instrux     Ictx->Instrux

    // One more byte has to follow, the modrm or the second VEX byte.
    RET_GT((ND_SIZET)Offset + 2, Size, ND_STATUS_BUFFER_TOO_SMALL);

    if ((ND_CODE_64 != Instrux->DefCode) && ((Code[Offset + 1] & 0xC0) != 0xC0))
    {
        return ND_STATUS_SUCCESS;
    }

    // VEX found, make sure the third byte is here.
    RET_GT((ND_SIZET)Offset + 3, Size, ND_STATUS_BUFFER_TOO_SMALL);

    // Make sure we don't have any other prefix.
    if (Instrux->HasOpSize || 
        Instrux->HasRepnzXacquireBnd ||
        Instrux->HasRepRepzXrelease || 
        Instrux->HasRex || 
        Instrux->HasRex2 || 
        Instrux->HasLock)
    {
        return ND_STATUS_VEX_WITH_PREFIX;
    }

    // Fill in XOP info.
    Instrux->VexMode = ND_VEXM_3B;
    Instrux->HasVex = ND_TRUE;
    Instrux->EncMode = ND_ENCM_VEX;
    Instrux->Vex3.Vex[0] = Code[Offset];
    Instrux->Vex3.Vex[1] = Code[Offset + 1];
    Instrux->Vex3.Vex[2] = Code[Offset + 2];

    Instrux->Exs.r = (ND_UINT32)~Instrux->Vex3.r;
    Instrux->Exs.x = (ND_UINT32)~Instrux->Vex3.x;
    Instrux->Exs.b = (ND_UINT32)~Instrux->Vex3.b;
    Instrux->Exs.m = Instrux->Vex3.m;
    Instrux->Exs.w = Instrux->Vex3.w;
    Instrux->Exs.v = (ND_UINT32)~Instrux->Vex3.v;
    Instrux->Exs.l = Instrux->Vex3.l;
    Instrux->Exs.p = Instrux->Vex3.p;

    // Do validations in case of VEX outside 64 bits.
    if (ND_CODE_64 != Instrux->DefCode)
    {
        // Vex.R and Vex.X have been tested by the initial if.

        // Vex.vvvv must be less than 8.
        Instrux->Exs.v &= 7;

        // Vex.B is ignored, so we force it to 0.
        Instrux->Exs.b = 0;
    }
    else
    {
        // Apply 64-bit operand size.
        if (Instrux->Exs.w == 1)
        {
            Instrux->OpMode = ND_OPSZ_64;
        }
    }

    // Update Instrux length & offset, and make sure we don't exceed 15 bytes.
    return NdIncrementLength(Instrux, 3);

#undef Instrux
#undef Context
}


//
// NdFetchEvex
//
static NDSTATUS
NdFetchEvex(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size
)
{
#define Context     Ictx->Context
#define Instrux     Ictx->Instrux

    // One more byte has to follow, the modrm or the second EVEX byte.
    RET_GT((ND_SIZET)Offset + 2, Size, ND_STATUS_BUFFER_TOO_SMALL);

    if ((ND_CODE_64 != Instrux->DefCode) && ((Code[Offset + 1] & 0xC0) != 0xC0))
    {
        // BOUND instruction in non-64 bit mode, not EVEX.
        return ND_STATUS_SUCCESS;
    }

    // EVEX found, make sure all the bytes are present. At least 4 bytes in total must be present.
    RET_GT((ND_SIZET)Offset + 4, Size, ND_STATUS_BUFFER_TOO_SMALL);

    // This is EVEX.
    Instrux->HasEvex = ND_TRUE;
    Instrux->EncMode = ND_ENCM_EVEX;
    Instrux->Evex.Evex[0] = Code[Offset + 0];
    Instrux->Evex.Evex[1] = Code[Offset + 1];
    Instrux->Evex.Evex[2] = Code[Offset + 2];
    Instrux->Evex.Evex[3] = Code[Offset + 3];

    // Legacy prefixes are not accepted with EVEX.
    if (Instrux->HasOpSize || 
        Instrux->HasRepnzXacquireBnd || 
        Instrux->HasRepRepzXrelease || 
        Instrux->HasRex || 
        Instrux->HasRex2 ||
        Instrux->HasLock)
    {
        return ND_STATUS_EVEX_WITH_PREFIX;
    }

    // Do the opcode independent checks. Opcode dependent checks are done when decoding each instruction.
    if (Instrux->Evex.m == 0)
    {
        return ND_STATUS_INVALID_ENCODING;
    }

    if (!(Context->FeatMode & ND_FEAT_APX))
    {
        // Check map. Maps 4 & 7 are allowed only if APX is enabled.
        if (Instrux->Evex.m == 4 || Instrux->Evex.m == 7)
        {
            return ND_STATUS_INVALID_ENCODING;
        }

        // For regular EVEX, B4 must be 0, and U must be 1.
        if (Instrux->Evex.b4 != 0 || Instrux->Evex.u != 1)
        {
            return ND_STATUS_INVALID_ENCODING;
        }
    }

    // Fill in the generic extension bits. We initially optimistically fill in all possible values.
    // Once we determine the opcode and, subsequently, the EVEX extension mode, we will do further 
    // validations, and reset unused fields to 0.
    Instrux->Exs.r = (ND_UINT32)~Instrux->Evex.r;
    Instrux->Exs.x = (ND_UINT32)~Instrux->Evex.x;
    Instrux->Exs.b = (ND_UINT32)~Instrux->Evex.b;
    Instrux->Exs.rp = (ND_UINT32)~Instrux->Evex.rp;
    Instrux->Exs.x4 = (ND_UINT32)~Instrux->Evex.u;
    Instrux->Exs.b4 = Instrux->Evex.b4;
    Instrux->Exs.m = Instrux->Evex.m;
    Instrux->Exs.w = Instrux->Evex.w;
    Instrux->Exs.v = (ND_UINT32)~Instrux->Evex.v;
    Instrux->Exs.vp = (ND_UINT32)~Instrux->Evex.vp;
    Instrux->Exs.p = Instrux->Evex.p;

    Instrux->Exs.z = Instrux->Evex.z;
    Instrux->Exs.l = Instrux->Evex.l;
    Instrux->Exs.bm = Instrux->Evex.bm;
    Instrux->Exs.k = Instrux->Evex.a;

    // EVEX extensions. The fields are undefined if the encoding does not use them.
    Instrux->Exs.nf = (Instrux->Evex.Evex[3] >> 2) & 1;
    Instrux->Exs.nd = (Instrux->Evex.Evex[3] >> 4) & 1;
    Instrux->Exs.sc = (Instrux->Evex.Evex[3] & 0xF);

    // Do EVEX validations outside 64 bits mode.
    if (ND_CODE_64 != Instrux->DefCode)
    {
        // Evex.R and Evex.X must be 1. If they're not, we have BOUND instruction. This is checked in the
        // first if. Note that they are inverted inside the Evex prefix.
        Instrux->Exs.r = 0;
        Instrux->Exs.x = 0;

        // Evex.B is ignored, so we force it to 0.
        Instrux->Exs.b = 0;

        // Evex.R' is ignored, so we force it to 0.
        Instrux->Exs.rp = 0;

        // Evex.B4 & Evex.X4 are ignored, so we force them to 0.
        Instrux->Exs.b4 = Instrux->Exs.x4 = 0;

        // High bit inside Evex.VVVV is ignored, so we force it to 0.
        Instrux->Exs.v &= 0x7;

        // Evex.V' must be 1 (negated to 0) in 32-bit mode.
        if (Instrux->Exs.vp == 1)
        {
            return ND_STATUS_BAD_EVEX_V_PRIME;
        }
    }
    else
    {
        // Apply 64-bit operand size.
        if (Instrux->Exs.w == 1)
        {
            Instrux->OpMode = ND_OPSZ_64;
        }
    }

    // Update Instrux length & offset, and make sure we don't exceed 15 bytes.
    return NdIncrementLength(Instrux, 4);

#undef Instrux
#undef Context
}


//
// NdFetchRex2
//
static NDSTATUS
NdFetchRex2(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size
)
{
#define Context     Ictx->Context
#define Instrux     Ictx->Instrux

    if (ND_CODE_64 != Instrux->DefCode)
    {
        // AAD instruction outside 64-bit mode.
        return ND_STATUS_SUCCESS;
    }

    if (!(Context->FeatMode & ND_FEAT_APX))
    {
        // APX not enabled, #UD.
        return ND_STATUS_SUCCESS;
    }

    if (Instrux->HasRex)
    {
        // REX illegal with REX2.
        return ND_STATUS_INVALID_PREFIX_SEQUENCE;
    }

    // One more byte has to follow + one opcode byte.
    RET_GT((ND_SIZET)Offset + 3, Size, ND_STATUS_BUFFER_TOO_SMALL);

    // This is REX2.
    Instrux->HasRex2 = ND_TRUE;
    Instrux->EncMode = ND_ENCM_LEGACY;
    Instrux->Rex2.Rex2[0] = Code[Offset + 0];
    Instrux->Rex2.Rex2[1] = Code[Offset + 1];

    // Fill in the generic extension bits
    Instrux->Exs.r = Instrux->Rex2.r3;
    Instrux->Exs.rp = Instrux->Rex2.r4;
    Instrux->Exs.x = Instrux->Rex2.x3;
    Instrux->Exs.x4 = Instrux->Rex2.x4;
    Instrux->Exs.b = Instrux->Rex2.b3;
    Instrux->Exs.b4 = Instrux->Rex2.b4;
    Instrux->Exs.w = Instrux->Rex2.w;
    Instrux->Exs.m = Instrux->Rex2.m0;

    // Make sure the opcode has support for REX2. In some cases, particular instructions selected by ModRm.reg
    // might not support REX2, but these are filtered during the decoding phase in NdDecodeInstruction.
    if (gRex2Table[Instrux->Rex2.m0][Code[Offset + 2]] == 0)
    {
        return ND_STATUS_INVALID_ENCODING;
    }

    // Toggle operand size to 64-bit mode.
    if (Instrux->Exs.w == 1)
    {
        Instrux->OpMode = ND_OPSZ_64;
    }

    // Update Instrux length & offset, and make sure we don't exceed 15 bytes.
    return NdIncrementLength(Instrux, 2);

#undef Instrux
#undef Context
}


//
// NdFetchPrefixes
//
static NDSTATUS
NdFetchPrefixes(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size
)
{
#define Context     Ictx->Context
#define Instrux     Ictx->Instrux

    const ND_UINT8 *prefMap = Instrux->DefCode == ND_CODE_64 ? gPrefixesMap64 : gPrefixesMap;
    NDSTATUS status = ND_STATUS_SUCCESS;;
    ND_UINT8 prefix;

    // We already validated that we have at least one available byte.
    while (prefMap[prefix = Code[Offset]] != ND_PREF_CODE_NONE)
    {
        if (ND_PREF_CODE_LEGACY == prefMap[prefix])
        {
            switch (prefix)
            {
            case ND_PREFIX_G0_LOCK:
                Instrux->HasLock = ND_TRUE;
                break;
            case ND_PREFIX_G1_REPE_REPZ:
                Instrux->Rep = ND_PREFIX_G1_REPE_REPZ;
                Instrux->HasRepRepzXrelease = ND_TRUE;
                break;
            case ND_PREFIX_G1_REPNE_REPNZ:
                Instrux->Rep = ND_PREFIX_G1_REPNE_REPNZ;
                Instrux->HasRepnzXacquireBnd = ND_TRUE;
                break;
            case ND_PREFIX_G2_SEG_CS:
            case ND_PREFIX_G2_SEG_SS:
            case ND_PREFIX_G2_SEG_DS:
            case ND_PREFIX_G2_SEG_ES:
            case ND_PREFIX_G2_SEG_FS:
            case ND_PREFIX_G2_SEG_GS:
                if (ND_CODE_64 == Instrux->DefCode)
                {
                    if (prefix == ND_PREFIX_G2_SEG_FS || 
                        prefix == ND_PREFIX_G2_SEG_GS)
                    {
                        // The last FS/GS is always used, if present.
                        Instrux->Seg = prefix;
                        Instrux->HasSeg = ND_TRUE;
                    }
                    else if (prefix == ND_PREFIX_G2_NO_TRACK && 
                        Instrux->Seg != ND_PREFIX_G2_SEG_FS &&
                        Instrux->Seg != ND_PREFIX_G2_SEG_GS)
                    {
                        // The Do Not Track prefix is considered only if there isn't a FS/GS prefix.
                        Instrux->Seg = prefix;
                        Instrux->HasSeg = ND_TRUE;
                    }
                    else if (Instrux->Seg != ND_PREFIX_G2_SEG_FS && 
                        Instrux->Seg != ND_PREFIX_G2_SEG_GS &&
                        Instrux->Seg != ND_PREFIX_G2_NO_TRACK)
                    {
                        // All other prefixes are considered if Do Not Track, FS, GS are not present.
                        Instrux->Seg = prefix;
                        Instrux->HasSeg = ND_TRUE;
                    }
                }
                else
                {
                    Instrux->Seg = prefix;
                    Instrux->HasSeg = ND_TRUE;
                }
                break;
            case ND_PREFIX_G3_OPERAND_SIZE:
                Instrux->HasOpSize = ND_TRUE;
                // Toggle operand size mode:
                // 16-bit mode: 32-bit operand size
                // 32-bit mode: 16-bit operand size
                // 64-bit mode: 16-bit operand size
                Instrux->OpMode = (Instrux->DefCode == ND_CODE_16) ? ND_OPSZ_32 : ND_OPSZ_16;
                break;
            case ND_PREFIX_G4_ADDR_SIZE:
                Instrux->HasAddrSize = ND_TRUE;
                // Toggle address size mode:
                // 16-bit mode: 32-bit address size
                // 32-bit mode: 16-bit address size
                // 64-bit mode: 32-bit address size
                Instrux->AddrMode = (Instrux->DefCode == ND_CODE_32) ? ND_ADDR_16 : ND_ADDR_32;
                break;
            default:
                break;
            }
        }
        else if (ND_PREF_CODE_REX == prefMap[prefix])
        {
            ND_SIZET next = (ND_SIZET)Offset + 1;

            // At least one opcode byte must follow a REX prefix.
            if (next >= Size)
            {
                return ND_STATUS_BUFFER_TOO_SMALL;
            }

            if (prefMap[Code[next]] != ND_PREF_CODE_LEGACY && prefMap[Code[next]] != ND_PREF_CODE_REX)
            {
                // REX is special, and the following rules apply:
                // 1. REX must be the last prefix before the opcode
                // 2. If any legacy (or another REX) prefix follows, this REX prefix will be ignored
                // 3. If a XOP/VEX/EVEX/REX2 prefix followed, #UD will be generated
                // Therefore, we digest the current REX prefix as long as another legacy/REX prefix does not follow.
                // If a XOP/VEX/EVEX/REX2 prefix followed, we will digest the current REX prefix, but we will return 
                // an error when we parse that XOP/VEX/EVEX/REX2 prefix.
                Instrux->HasRex = ND_TRUE;
                Instrux->Rex.Rex = prefix;
                Instrux->Exs.w = Instrux->Rex.w;
                Instrux->Exs.r = Instrux->Rex.r;
                Instrux->Exs.x = Instrux->Rex.x;
                Instrux->Exs.b = Instrux->Rex.b;

                // Toggle operand size to 64-bit mode.
                if (Instrux->Exs.w == 1)
                {
                    Instrux->OpMode = ND_OPSZ_64;
                }
            }
        }
        else if (ND_PREF_CODE_EX == prefMap[prefix])
        {
            switch (prefix)
            {
            case ND_PREFIX_XOP:
                status = NdFetchXop(Ictx, Code, Offset, Size);
                break;
            case ND_PREFIX_VEX_2B:
                status = NdFetchVex2(Ictx, Code, Offset, Size);
                break;
            case ND_PREFIX_VEX_3B:
                status = NdFetchVex3(Ictx, Code, Offset, Size);
                break;
            case ND_PREFIX_EVEX:
                status = NdFetchEvex(Ictx, Code, Offset, Size);
                break;
            case ND_PREFIX_REX2:
                status = NdFetchRex2(Ictx, Code, Offset, Size);
                break;
            default:
                status = ND_STATUS_INVALID_INSTRUX;
            }

            // Must be the last prefix before the opcode, so we break here; the next byte must be the opcode.
            break;
        }

        // We have found prefixes, update the instruction length and the current offset.
        Instrux->Length++, Offset++;
        if (Instrux->Length > ND_MAX_INSTRUCTION_LENGTH)
        {
            return ND_STATUS_INSTRUCTION_TOO_LONG;
        }

        // At least one more byte must be available.
        RET_GT((ND_SIZET)Offset + 1, Size, ND_STATUS_BUFFER_TOO_SMALL);
    }

    // The total length of the instruction is the total length of the prefixes right now.
    Instrux->PrefLength = Instrux->OpOffset = Instrux->MainOpOffset = Instrux->Length;

    return status;

#undef Instrux
#undef Context
}


//
// NdFetchOpcodes
//
static NDSTATUS
NdFetchOpcodes(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size
)
{
#define Context     Ictx->Context
#define Instrux     Ictx->Instrux

    // Fetch first opcode.
    RET_GT((ND_SIZET)Offset + 1, Size, ND_STATUS_BUFFER_TOO_SMALL);
    Instrux->PrimaryOpCode = Code[Offset++], Instrux->OpLength++;

    // Fetch subsequent opcodes, if the curent one is an escape opcode.
    if (Instrux->PrimaryOpCode == 0x0F && Instrux->EncMode == ND_ENCM_LEGACY && !Instrux->HasRex2)
    {
        Instrux->Exs.m = 1;

        RET_GT((ND_SIZET)Offset + 1, Size, ND_STATUS_BUFFER_TOO_SMALL);
        Instrux->PrimaryOpCode = Code[Offset++], Instrux->OpLength++, Instrux->MainOpOffset++;

        if (Instrux->PrimaryOpCode == 0x38)
        {
            Instrux->Exs.m = 2;

            RET_GT((ND_SIZET)Offset + 1, Size, ND_STATUS_BUFFER_TOO_SMALL);
            Instrux->PrimaryOpCode = Code[Offset++], Instrux->OpLength++, Instrux->MainOpOffset++;
        }
        else if (Instrux->PrimaryOpCode == 0x3A)
        {
            Instrux->Exs.m = 3;

            RET_GT((ND_SIZET)Offset + 1, Size, ND_STATUS_BUFFER_TOO_SMALL);
            Instrux->PrimaryOpCode = Code[Offset++], Instrux->OpLength++, Instrux->MainOpOffset++;
        }
    }

    return NdIncrementLength(Instrux, Instrux->OpLength);

#undef Instrux
#undef Context
}


//
// NdFetchOpcode
//
static NDSTATUS
NdFetchOpcode(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size
)
{
#define Instrux     Ictx->Instrux

    // At least one byte must be available, for the fetched opcode.
    RET_GT((ND_SIZET)Offset + 1, Size, ND_STATUS_BUFFER_TOO_SMALL);

    Instrux->OpLength++;

    Instrux->PrimaryOpCode = Code[Offset];

    return NdIncrementLength(Instrux, 1);

#undef Instrux
}


//
// NdFetchModrm
//
static NDSTATUS
NdFetchModrm(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size
)
{
#define Instrux     Ictx->Instrux

    // At least one byte must be available, for the modrm byte.
    RET_GT((ND_SIZET)Offset + 1, Size, ND_STATUS_BUFFER_TOO_SMALL);

    // If we get called, we assume we have ModRM.
    Instrux->HasModRm = ND_TRUE;
    Instrux->ModRmOffset = Offset;

    // Fetch the ModRM byte & update the offset and the instruction length.
    Instrux->ModRm.ModRm = Code[Offset];

    return NdIncrementLength(Instrux, 1);

#undef Instrux
}


//
// NdFetchSib
//
static NDSTATUS
NdFetchSib(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size
)
{
#define Instrux     Ictx->Instrux

    // At least one more byte must be available, for the sib.
    RET_GT((ND_SIZET)Offset + 1, Size, ND_STATUS_BUFFER_TOO_SMALL);

    // SIB present.
    Instrux->HasSib = ND_TRUE;

    Instrux->Sib.Sib = Code[Offset];
    
    return NdIncrementLength(Instrux, 1);

#undef Instrux
}


//
// NdFetchDisplacement
//
static NDSTATUS
NdFetchDisplacement(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size,
    ND_UINT8 DispSize
)
{
#define Instrux     Ictx->Instrux

    // Make sure enough buffer space is available.
    RET_GT((ND_SIZET)Offset + DispSize, Size, ND_STATUS_BUFFER_TOO_SMALL);

    // If we get here, we have displacement.
    Instrux->HasDisp = ND_TRUE;
    Instrux->DispLength = DispSize;
    Instrux->DispOffset = Offset;

    switch (DispSize)
    {
    case 1:
        Instrux->Displacement = ND_FETCH_8(Code + Offset);
        break;
    case 2:
        Instrux->Displacement = ND_FETCH_16(Code + Offset);
        break;
    case 4:
        Instrux->Displacement = ND_FETCH_32(Code + Offset);
        break;
    default:
        return ND_STATUS_INVALID_PARAMETER;
    }

    Instrux->IsRipRelative = (ND_CODE_64 == Instrux->DefCode) && 
                             (Instrux->ModRm.mod == 0) && 
                             (Instrux->ModRm.rm == NDR_RBP);

    return NdIncrementLength(Instrux, DispSize);

#undef Instrux
}


//
// NdFetchModrmSibDisplacement
//
static NDSTATUS
NdFetchModrmSibDisplacement(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size
)
{
    NDSTATUS status;
    ND_UINT8 base, modrm;

    // Make sure a ModRm table entry exists for the encoding. A table may be missing if we end up with an 
    // unknown/unssuported map id.
    if (ND_NULL == gModrmTable[Ictx->Instrux->EncMode][Ictx->Instrux->Exs.m])
    {
        return ND_STATUS_INVALID_ENCODING;
    }

    modrm = gModrmTable[Ictx->Instrux->EncMode][Ictx->Instrux->Exs.m][Ictx->Instrux->PrimaryOpCode];
    if (modrm == 0)
    {
        return ND_STATUS_SUCCESS;
    }

    // Fetch ModRm.
    status = NdFetchModrm(Ictx, Code, Offset++, Size);
    if (!ND_SUCCESS(status))
    {
        return status;
    }

    // Special case for instructions that always force the mode to register. In that case (such as DR and CR access
    // instructions), there is no memory mode, and hence no SIB and no displacement.
    if (modrm == 2)
    {
        return ND_STATUS_SUCCESS;
    }

    base = Ictx->Instrux->ModRm.rm;

    // If needed, fetch the SIB.
    if ((Ictx->Instrux->ModRm.rm == NDR_RSP) && 
        (Ictx->Instrux->ModRm.mod != 3) && 
        (Ictx->Instrux->AddrMode != ND_ADDR_16))
    {
        status = NdFetchSib(Ictx, Code, Offset++, Size);
        if (!ND_SUCCESS(status))
        {
            return status;
        }

        base = Ictx->Instrux->Sib.base;
    }

    // If needed, fetch displacement.
    if ((Ictx->Instrux->ModRm.mod == 0 && base == 5 && Ictx->Instrux->AddrMode != ND_ADDR_16) ||
        (Ictx->Instrux->ModRm.mod == 0 && base == 6 && Ictx->Instrux->AddrMode == ND_ADDR_16) ||
        (Ictx->Instrux->ModRm.mod == 1) ||
        (Ictx->Instrux->ModRm.mod == 2))
    {
        ND_UINT8 dispSize;

        if (Ictx->Instrux->AddrMode == ND_ADDR_16)
        {
            dispSize = gDispsizemap16[Ictx->Instrux->ModRm.mod][base];
        }
        else
        {
            dispSize = gDispsizemap[Ictx->Instrux->ModRm.mod][base];
        }

        status = NdFetchDisplacement(Ictx, Code, Offset, Size, dispSize);
        if (!ND_SUCCESS(status))
        {
            return status;
        }
    }

    return ND_STATUS_SUCCESS;
}


//
// NdFetchAddressFar
//
static NDSTATUS
NdFetchAddressFar(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size,
    ND_UINT8 AddressSize
)
{
#define Instrux     Ictx->Instrux

    RET_GT((ND_SIZET)Offset + AddressSize, Size, ND_STATUS_BUFFER_TOO_SMALL);

    Instrux->HasAddr = ND_TRUE;
    Instrux->AddrLength = AddressSize;
    Instrux->AddrOffset = Offset;

    switch (AddressSize)
    {
    case 4:
        Instrux->Address.Ip = ND_FETCH_16(Code + Offset);
        Instrux->Address.Cs = ND_FETCH_16(Code + Offset + 2);
        break;
    case 6:
        Instrux->Address.Ip = ND_FETCH_32(Code + Offset);
        Instrux->Address.Cs = ND_FETCH_16(Code + Offset + 4);
        break;
    default:
        return ND_STATUS_INVALID_PARAMETER;
    }

    return NdIncrementLength(Instrux, AddressSize);

#undef Instrux
}


//
// NdFetchAddressNear
//
static NDSTATUS
NdFetchAddressNear(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size,
    ND_UINT8 AddressSize
)
{
#define Instrux     Ictx->Instrux

    RET_GT((ND_SIZET)Offset + AddressSize, Size, ND_STATUS_BUFFER_TOO_SMALL);

    Instrux->HasAddrNear = ND_TRUE;
    Instrux->AddrLength = AddressSize;
    Instrux->AddrOffset = Offset;

    if (AddressSize == 8)
    {
        Instrux->AddressNear = ND_FETCH_64(Code + Offset);
    }
    else
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    return NdIncrementLength(Instrux, AddressSize);

#undef Instrux
}


//
// NdFetchImmediate
//
static NDSTATUS
NdFetchImmediate(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size,
    ND_UINT8 ImmediateSize
)
{
#define Instrux     Ictx->Instrux

    RET_GT((ND_SIZET)Offset + ImmediateSize, Size, ND_STATUS_BUFFER_TOO_SMALL);

    Instrux->HasImm1 = ND_TRUE;
    Instrux->Imm1Length = ImmediateSize;
    Instrux->Imm1Offset = Offset;

    switch (ImmediateSize)
    {
    case 1:
        Instrux->Immediate1 = ND_FETCH_8(Code + Offset);
        break;
    case 2:
        Instrux->Immediate1 = ND_FETCH_16(Code + Offset);
        break;
    case 4:
        Instrux->Immediate1 = ND_FETCH_32(Code + Offset);
        break;
    case 8:
        Instrux->Immediate1 = ND_FETCH_64(Code + Offset);
        break;
    default:
        return ND_STATUS_INVALID_PARAMETER;
    }

    return NdIncrementLength(Instrux, ImmediateSize);

#undef Instrux
}

//
// NdFetchImmediate2
//
static NDSTATUS
NdFetchImmediate2(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size,
    ND_UINT8 ImmediateSize
)
{
#define Instrux     Ictx->Instrux

    RET_GT((ND_SIZET)Offset + ImmediateSize, Size, ND_STATUS_BUFFER_TOO_SMALL);

    Instrux->HasImm2 = ND_TRUE;
    Instrux->Imm2Length = ImmediateSize;
    Instrux->Imm2Offset = Offset;

    if (ImmediateSize == 1)
    {
        Instrux->Immediate2 = ND_FETCH_8(Code + Offset);
    }
    else
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    return NdIncrementLength(Instrux, ImmediateSize);

#undef Instrux
}

//
// NdFetchImmediates
//
static NDSTATUS
NdFetchImmediates(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size,
    ND_UINT8 Immediate1Size,
    ND_UINT8 Immediate2Size
)
{
    NDSTATUS status;

    status = NdFetchImmediate(Ictx, Code, Offset, Size, Immediate1Size);
    if (!ND_SUCCESS(status))
    {
        return status;
    }

    status = NdFetchImmediate2(Ictx, Code, Offset + Immediate1Size, Size, Immediate2Size);
    if (!ND_SUCCESS(status))
    {
        return status;
    }

    return ND_STATUS_SUCCESS;
}

//
// NdFetchRelativeOffset
//
static NDSTATUS
NdFetchRelativeOffset(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size,
    ND_UINT8 RelOffsetSize
)
{
#define Instrux     Ictx->Instrux

    // Make sure we don't outrun the buffer.
    RET_GT((ND_SIZET)Offset + RelOffsetSize, Size, ND_STATUS_BUFFER_TOO_SMALL);

    Instrux->HasRelOffs = ND_TRUE;
    Instrux->RelOffsLength = RelOffsetSize;
    Instrux->RelOffsOffset = Offset;

    switch (RelOffsetSize)
    {
    case 1:
        Instrux->RelativeOffset = ND_FETCH_8(Code + Offset);
        break;
    case 2:
        Instrux->RelativeOffset = ND_FETCH_16(Code + Offset);
        break;
    case 4:
        Instrux->RelativeOffset = ND_FETCH_32(Code + Offset);
        break;
    default:
        return ND_STATUS_INVALID_PARAMETER;
    }

    Instrux->IsRipRelative = ND_TRUE;

    return NdIncrementLength(Instrux, RelOffsetSize);

#undef Instrux
}


//
// NdFetchMoffset
//
static NDSTATUS
NdFetchMoffset(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size,
    ND_UINT8 MoffsetSize
)
{
#define Instrux     Ictx->Instrux

    RET_GT((ND_SIZET)Offset + MoffsetSize, Size, ND_STATUS_BUFFER_TOO_SMALL);

    Instrux->HasMoffset = ND_TRUE;
    Instrux->MoffsetLength = MoffsetSize;
    Instrux->MoffsetOffset = Offset;

    switch (MoffsetSize)
    {
    case 2:
        Instrux->Moffset = ND_FETCH_16(Code + Offset);
        break;
    case 4:
        Instrux->Moffset = ND_FETCH_32(Code + Offset);
        break;
    case 8:
        Instrux->Moffset = ND_FETCH_64(Code + Offset);
        break;
    default:
        return ND_STATUS_INVALID_PARAMETER;
    }

    return NdIncrementLength(Instrux, MoffsetSize);

#undef Instrux
}


//
// NdFetchSseImmediate
//
static NDSTATUS
NdFetchSseImmediate(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size,
    ND_UINT8 SseImmSize
)
{
#define Instrux     Ictx->Instrux

    RET_GT((ND_SIZET)Offset + SseImmSize, Size, ND_STATUS_BUFFER_TOO_SMALL);

    Instrux->HasSseImm = ND_TRUE;
    Instrux->SseImmOffset = Offset;

    Instrux->SseImmediate = ND_FETCH_8(Code + Offset);

    return NdIncrementLength(Instrux, SseImmSize);

#undef Instrux
}


//
// NdFetchPayloadBytes
//
static NDSTATUS
NdFetchPayloadBytes(
    const ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_UINT8 Offset,
    ND_SIZET Size
)
{
    static const ND_UINT8 zszLut[4] = { 2, 4, 4,  0 };
    static const ND_UINT8 vszLut[4] = { 2, 4, 8,  0 };
    static const ND_UINT8 pszLut[4] = { 4, 6, 10, 0 };

    switch (Ictx->Idbe->Ipb)
    {
    case ND_IPB_I_b:
        return NdFetchImmediate(Ictx, Code, Offset, Size, 1);
    case ND_IPB_I_w:
        return NdFetchImmediate(Ictx, Code, Offset, Size, 2);
    case ND_IPB_I_d:
        return NdFetchImmediate(Ictx, Code, Offset, Size, 4);
    case ND_IPB_I_v:
        return NdFetchImmediate(Ictx, Code, Offset, Size, vszLut[Ictx->Instrux->EfOpMode]);
    case ND_IPB_I_z:
        return NdFetchImmediate(Ictx, Code, Offset, Size, zszLut[Ictx->Instrux->EfOpMode]);
    case ND_IPB_I_wb:
        return NdFetchImmediates(Ictx, Code, Offset, Size, 2, 1);
    case ND_IPB_I_bb:
        return NdFetchImmediates(Ictx, Code, Offset, Size, 1, 1);
    case ND_IPB_J_b:
        return NdFetchRelativeOffset(Ictx, Code, Offset, Size, 1);
    case ND_IPB_J_z:
        return NdFetchRelativeOffset(Ictx, Code, Offset, Size, zszLut[Ictx->Instrux->EfOpMode]);
    case ND_IPB_A_p:
        return NdFetchAddressFar(Ictx, Code, Offset, Size, pszLut[Ictx->Instrux->EfOpMode]);
    case ND_IPB_A_q:
        return NdFetchAddressNear(Ictx, Code, Offset, Size, 8);
    case ND_IPB_O_a:
        return NdFetchMoffset(Ictx, Code, Offset, Size, vszLut[Ictx->Instrux->AddrMode]);
    case ND_IPB_L_b:
        return NdFetchSseImmediate(Ictx, Code, Offset, Size, 1);
    default:
        return ND_STATUS_SUCCESS;
    }
}


//
// NdDecodeInstruction
//
static NDSTATUS
NdDecodeInstruction(
    ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_SIZET Size,
    const ND_IDBE **InsDef
)
{
#define Context     Ictx->Context
#define Instrux     Ictx->Instrux

    NDSTATUS status = ND_STATUS_INVALID_ENCODING;
    const ND_TABLE *pTable = ND_NULL;

    // Locate the main opcode table, depending on encoding mode and map ID.
    switch (Instrux->EncMode)
    {
    case ND_ENCM_LEGACY:
        pTable = (const ND_TABLE *)gLegacyMap_mmmmm.Table[Instrux->Exs.m];
        break;
    case ND_ENCM_XOP:
        pTable = (const ND_TABLE *)gXopMap_mmmmm.Table[Instrux->Exs.m];
        break;
    case ND_ENCM_VEX:
        pTable = (const ND_TABLE *)gVexMap_mmmmm.Table[Instrux->Exs.m];
        break;
    case ND_ENCM_EVEX:
        pTable = (const ND_TABLE *)gEvexMap_mmmmm.Table[Instrux->Exs.m];
        break;
    default:
        pTable = (const ND_TABLE *)ND_NULL;
        break;
    }

    // Make sure the table exists & lookup the opcode entry.
    if (pTable == ND_NULL || pTable->Type != ND_ILUT_OPCODE)
    {
        return ND_STATUS_INVALID_ENCODING;
    }

    pTable = pTable->Table[Instrux->PrimaryOpCode];

    while (pTable != ND_NULL && pTable->Type != ND_ILUT_INSTRUCTION)
    {
        switch (pTable->Type)
        {
        // We don't need to check for the opcode tables any longer, since we fetch all the opcodes in one step, 
        // during NdFetchOpcodes, and then we lookup directly the appropiate opcode table. However, the mechanism
        // remain in place, and all the tables are still chained together as before. Uncomment the case below if
        // at any moment the legacy behavior is desired.
        //case ND_ILUT_OPCODE:
        //    // We need an opcode to keep going.
        //    status = NdFetchOpcode(Ictx, Code, Instrux->Length, Size);
        //    if (!ND_SUCCESS(status))
        //    {
        //        return status;
        //    }
        //
        //    pTable = (const ND_TABLE *)pTable->Table[Instrux->PrimaryOpCode];
        //    break;

        case ND_ILUT_OPCODE_LAST:
            // Fetch the opcode, which is after the modrm and displacement.
            status = NdFetchOpcode(Ictx, Code, Instrux->Length, Size);
            if (!ND_SUCCESS(status))
            {
                return status;
            }

            // Special case for instructions that encode the main opcode as the last byte (3dNow!).
            Instrux->MainOpOffset = Instrux->Length - 1;

            pTable = (const ND_TABLE *)pTable->Table[Instrux->PrimaryOpCode];
            break;

        case ND_ILUT_MODRM_MOD:
            // Next index is either 0 (mem) or 1 (reg)
            pTable = (const ND_TABLE *)pTable->Table[Instrux->ModRm.mod == 3 ? 1 : 0];
            break;

        case ND_ILUT_MODRM_REG:
            // Next index is the reg.
            pTable = (const ND_TABLE *)pTable->Table[Instrux->ModRm.reg];
            break;

        case ND_ILUT_MODRM_RM:
            // Next index is the rm.
            pTable = (const ND_TABLE *)pTable->Table[Instrux->ModRm.rm];
            break;

        case ND_ILUT_MAN_PREFIX:
            // We have mandatory prefixes.
            if (Instrux->Rep == 0xF2)
            {
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_MAN_PREF_F2];
                Instrux->HasMandatoryF2 = ND_TRUE;
            }
            else if (Instrux->Rep == 0xF3)
            {
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_MAN_PREF_F3];
                Instrux->HasMandatoryF3 = ND_TRUE;
            }
            else if (Instrux->HasOpSize)
            {
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_MAN_PREF_66];
                Instrux->HasMandatory66 = ND_TRUE;
            }
            else
            {
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_MAN_PREF_NP];
            }
            break;

        case ND_ILUT_MODE:
            if (Instrux->DefCode > ND_CODE_64)
            {
                return ND_STATUS_INVALID_INSTRUX;
            }

            if (ND_NULL != pTable->Table[Instrux->DefCode + 1])
            {
                pTable = (const ND_TABLE *)pTable->Table[Instrux->DefCode + 1];
            }
            else
            {
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_MODE_NONE];
            }
            break;

        case ND_ILUT_DSIZE:
            if (Instrux->OpMode > ND_OPSZ_64)
            {
                return ND_STATUS_INVALID_INSTRUX;
            }

            // Handle default/forced redirections in 64 bit mode.
            if (ND_CODE_64 == Instrux->DefCode)
            {
                // 64-bit mode, we may have forced/default operand sizes.
                if ((ND_NULL != pTable->Table[ND_ILUT_INDEX_DSIZE_D64]) && (!Instrux->HasOpSize || Instrux->Exs.w))
                {
                    pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_DSIZE_D64];
                }
                else if (ND_NULL != pTable->Table[ND_ILUT_INDEX_DSIZE_F64])
                {
                    pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_DSIZE_F64];
                }
                else if (ND_NULL != pTable->Table[Instrux->OpMode + 1])
                {
                    pTable = (const ND_TABLE *)pTable->Table[Instrux->OpMode + 1];
                }
                else
                {
                    pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_DSIZE_NONE];
                }
            }
            else if (ND_NULL != pTable->Table[Instrux->OpMode + 1])
            {
                pTable = (const ND_TABLE *)pTable->Table[Instrux->OpMode + 1];
            }
            else
            {
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_DSIZE_NONE];
            }
            break;

        case ND_ILUT_ASIZE:
            if (Instrux->AddrMode > ND_ADDR_64)
            {
                return ND_STATUS_INVALID_INSTRUX;
            }

            if (ND_NULL != pTable->Table[Instrux->AddrMode + 1])
            {
                pTable = (const ND_TABLE *)pTable->Table[Instrux->AddrMode + 1];
            }
            else
            {
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_ASIZE_NONE];
            }
            break;

        case ND_ILUT_AUXILIARY:
            // Auxiliary redirection. Default to table[0] if nothing matches.
            if (Instrux->Rep == ND_PREFIX_G1_REPE_REPZ && (ND_NULL != pTable->Table[ND_ILUT_INDEX_AUX_REPZ]))
            {
                // Instruction contains 0xF3.
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_AUX_REPZ];
            }
            else if ((Instrux->Rep != 0) && (ND_NULL != pTable->Table[ND_ILUT_INDEX_AUX_REP]))
            {
                // Instruction contains either 0xF2 or 0xF3.
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_AUX_REP];
            }
            else if ((Instrux->Exs.b || Instrux->Exs.b4) && (ND_NULL != pTable->Table[ND_ILUT_INDEX_AUX_REXB]))
            {
                // Instruction contains REX or REX2 B3/B4.
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_AUX_REXB];
            }
            else if (Instrux->Exs.w && (ND_NULL != pTable->Table[ND_ILUT_INDEX_AUX_REXW]))
            {
                // Instructions contains RWX or REX2 W.
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_AUX_REXW];
            }
            else if ((Instrux->DefCode == ND_CODE_64) && (ND_NULL != pTable->Table[ND_ILUT_INDEX_AUX_MO64]))
            {
                // Instruction valid only in 64-bit mode.
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_AUX_MO64];
            }
            else if (Instrux->IsRipRelative && ND_NULL != pTable->Table[ND_ILUT_INDEX_AUX_RIPREL])
            {
                // Instruction contains RIP relative addressing.
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_AUX_RIPREL];
            }
            else if (Instrux->HasRex2 && (ND_NULL != pTable->Table[ND_ILUT_INDEX_AUX_REX2]))
            {
                // Instruction contains REX2.
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_AUX_REX2];
            }
            else if (Instrux->HasRex2 && Instrux->Rex2.w && (ND_NULL != pTable->Table[ND_ILUT_INDEX_AUX_REX2W]))
            {
                // Instruction contains REX2.W.
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_AUX_REX2W];
            }
            else
            {
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_INDEX_AUX_NONE];
            }
            break;

        case ND_ILUT_VENDOR:
            // No need for VendMode range checks, as it is limited by the data type representation - 4 bits.
            //if (Context->VendMode > ND_ILUT_SIZE_VENDOR)
            //{
            //    return ND_STATUS_INVALID_INSTRUX;
            //}

            // Vendor redirection. Go to the vendor specific entry.
            if (ND_NULL != pTable->Table[Context->VendMode])
            {
                pTable = (const ND_TABLE *)pTable->Table[Context->VendMode];
            }
            else
            {
                pTable = (const ND_TABLE *)pTable->Table[ND_VEND_ANY];
            }
            break;

        case ND_ILUT_FEATURE:
            // Feature redirection. Normally NOP if feature is not set, but may be something else if feature is set.
            if ((ND_NULL != pTable->Table[ND_ILUT_FEATURE_MPX]) && !!(Context->FeatMode & ND_FEAT_MPX))
            {
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_FEATURE_MPX];
            }
            else if ((ND_NULL != pTable->Table[ND_ILUT_FEATURE_CET]) && !!(Context->FeatMode & ND_FEAT_CET))
            {
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_FEATURE_CET];
            }
            else if ((ND_NULL != pTable->Table[ND_ILUT_FEATURE_CLDEMOTE]) && !!(Context->FeatMode & ND_FEAT_CLDEMOTE))
            {
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_FEATURE_CLDEMOTE];
            }
            else if ((ND_NULL != pTable->Table[ND_ILUT_FEATURE_PITI]) && !!(Context->FeatMode & ND_FEAT_PITI))
            {
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_FEATURE_PITI];
            }
            else if ((ND_NULL != pTable->Table[ND_ILUT_FEATURE_MOVRS]) && !!(Context->FeatMode & ND_FEAT_MOVRS))
            {
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_FEATURE_MOVRS];
            }
            else if ((ND_NULL != pTable->Table[ND_ILUT_FEATURE_BHI]) && !!(Context->FeatMode & ND_FEAT_BHI))
            {
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_FEATURE_BHI];
            }
            else
            {
                pTable = (const ND_TABLE *)pTable->Table[ND_ILUT_FEATURE_NONE];
            }
            break;

        case ND_ILUT_EX_M:
            pTable = (const ND_TABLE *)pTable->Table[Instrux->Exs.m];
            break;

        case ND_ILUT_EX_PP:
            pTable = (const ND_TABLE *)pTable->Table[Instrux->Exs.p];
            break;

        case ND_ILUT_EX_L:
            if (Instrux->HasEvex && Instrux->Exs.m != 4 && Instrux->Exs.bm && 3 == Instrux->ModRm.mod)
            {
                // We have evex; we need to test the modrm now, because we have to make sure we don't have SAE or ER;
                // if we do have SAE or ER, we have to check the modrm byte and see if it is a reg-reg form (mod = 3),
                // in which case L'L is forced to the maximum vector length of the instruction. We know for sure that
                // all EVEX instructions have modrm.
                // Skip these checks for EVEX map 4, which are legacy instructions promoted to EVEX, and which do not
                // support SAE, ER or broadcast.
                // Otherwise, we use the maximum vector length of the instruction. If the instruction does not support
                // SAE or ER, a #UD would be generated. We check for this later.
                if (ND_NULL != pTable->Table[2])
                {
                    pTable = (const ND_TABLE *)pTable->Table[2];
                }
                else if (ND_NULL != pTable->Table[1])
                {
                    pTable = (const ND_TABLE *)pTable->Table[1];
                }
                else
                {
                    pTable = (const ND_TABLE *)pTable->Table[0];
                }
            }
            else
            {
                pTable = (const ND_TABLE *)pTable->Table[Instrux->Exs.l];
            }
            break;

        case ND_ILUT_EX_W:
            pTable = (const ND_TABLE *)pTable->Table[Instrux->Exs.w];
            break;

        case ND_ILUT_EX_WI:
            pTable = (const ND_TABLE *)pTable->Table[Instrux->DefCode == ND_CODE_64 ? Instrux->Exs.w : 0];
            break;

        case ND_ILUT_EX_ND:
            // New data modified field encoded in EVEX payload byte 3.
            pTable = (const ND_TABLE *)pTable->Table[Instrux->Exs.nd];
            break;

        case ND_ILUT_EX_NF:
            // No flags modifier field encoded in EVEX payload byte 3.
            pTable = (const ND_TABLE *)pTable->Table[Instrux->Exs.nf];
            break;

        case ND_ILUT_EX_SC:
            // Standard condition field encoded in EVEX payload byte 3.
            pTable = (const ND_TABLE *)pTable->Table[Instrux->Exs.sc];
            break;

        case ND_ILUT_EX_LPDF:
            // Compressed index, comprising of L'L + pp + nd + nf. Speeds up lookup for EVEX promoted legacy 
            // instructions, since we only have to do a single lookup instead of 4.
            pTable = (const ND_TABLE *)pTable->Table[Instrux->Exs.l  << 4 | 
                                                     Instrux->Exs.p  << 2 | 
                                                     Instrux->Exs.nd << 1 | 
                                                     Instrux->Exs.nf];
            break;

        case ND_ILUT_FLT_NO64:
            // Instruction invalid in 64-bit mode.
            if (Instrux->DefCode != ND_CODE_64)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_ENCODING_IN_MODE;
            }
            break;

        case ND_ILUT_FLT_NO1632:
            // Instruction invalid outside 64-bit mode.
            if (Instrux->DefCode == ND_CODE_64)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_ENCODING_IN_MODE;
            }
            break;

        case ND_ILUT_FLT_NORIPREL:
            // Instruction must not be RIP-relative.
            if (!Instrux->IsRipRelative)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_RIP_REL_ADDRESSING_NOT_SUPPORTED;
            }
            break;

        case ND_ILUT_FLT_NOA16:
            // Instruction must not use 16-bit addressing.
            if (Instrux->AddrMode != ND_ADDR_16)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_16_BIT_ADDRESSING_NOT_SUPPORTED;
            }
            break;

        case ND_ILUT_FLT_NO66:
            // Instruction must not use the 0x66 prefix.
            if (!Instrux->HasOpSize)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_ENCODING;
            }
            break;

        case ND_ILUT_FLT_NO67:
            // Instruction must not use the 0x67 prefix.
            if (!Instrux->HasAddrSize)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_ENCODING;
            }
            break;

        case ND_ILUT_FLT_NOREP:
            // Instruction must not use any REP prefix.
            if (Instrux->Rep == 0)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_ENCODING;
            }
            break;

        case ND_ILUT_FLT_NOREX2:
            // Instruction must not use REX2 prefix.
            if (!Instrux->HasRex2)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_ENCODING;
            }
            break;

        case ND_ILUT_FLT_NOL0:
            // Instruction not valid with L'L or L equal to 0.
            if (Instrux->Exs.l != 0)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_ENCODING;
            }
            break;

        case ND_ILUT_FLT_NOV:
            // Instruction not valid if VVVV is not 0.
            if (Instrux->Exs.v == 0)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_VEX_VVVV_MUST_BE_ZERO;
            }
            break;

        case ND_ILUT_FLT_NOVP:
            // Instruction not valid if V' is not 0.
            if (Instrux->Exs.vp == 0)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_BAD_EVEX_V_PRIME;
            }
            break;

        case ND_ILUT_FLT_NOVVP:
            // Instruction not valid if VVVV is not 0 or V' is not 0.
            if (Instrux->Exs.vp == 0 && Instrux->Exs.v == 0)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else 
            {
                if (Instrux->Exs.v != 0)
                {
                    return ND_STATUS_VEX_VVVV_MUST_BE_ZERO;
                }
                else
                {
                    return ND_STATUS_BAD_EVEX_V_PRIME;
                }
            }
            break;

        case ND_ILUT_FLT_RRLT16:
            // GPR encoded in R, when APX is disabled, must be less than 16.
            if (!!(Context->FeatMode & ND_FEAT_APX) || ND_DECODE_GPR_R(Instrux) < 16)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_REGISTER_IN_INSTRUCTION;
            }
            break;

        case ND_ILUT_FLT_RVLT16:
            // GPR encoded in V, when APX is disabled, must be less than 16.
            if (!!(Context->FeatMode & ND_FEAT_APX) || ND_DECODE_GPR_V(Instrux) < 16)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_REGISTER_IN_INSTRUCTION;
            }
            break;

        case ND_ILUT_FLT_SRIN012345:
            // Segment register encoded in R must be one of 0, 1, 2, 3, 4, 5.
            if (ND_DECODE_SEG_R(Instrux) < 6)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_REGISTER_IN_INSTRUCTION;
            }
            break;

        case ND_ILUT_FLT_SRIN02345:
            // Segment register encoded in R must be one of 0, 2, 3, 4, 5.
            if (ND_DECODE_SEG_R(Instrux) < 6 && ND_DECODE_SEG_R(Instrux) != 1)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                if (ND_DECODE_SEG_R(Instrux) == 1)
                {
                    return ND_STATUS_CS_LOAD;
                }
                else
                {
                    return ND_STATUS_INVALID_REGISTER_IN_INSTRUCTION;
                }
            }
            break;

        case ND_ILUT_FLT_BRLT4:
            // Bound register encoded in R must be less than 4.
            if (ND_DECODE_BND_R(Instrux) < ND_MAX_BND_REGS)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_REGISTER_IN_INSTRUCTION;
            }
            break;

        case ND_ILUT_FLT_BMLT4:
            // Bound register encoded in M must be less than 4.
            if (ND_DECODE_BND_M(Instrux) < ND_MAX_BND_REGS || Instrux->ModRm.mod != 3)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_REGISTER_IN_INSTRUCTION;
            }
            break;


        case ND_ILUT_FLT_CRIN02348:
            // Control register encoded in R must be one of 0, 2, 3, 4, 8.
            if (ND_DECODE_CR_R(Instrux) == 0 ||
                ND_DECODE_CR_R(Instrux) == 2 ||
                ND_DECODE_CR_R(Instrux) == 3 ||
                ND_DECODE_CR_R(Instrux) == 4 ||
                ND_DECODE_CR_R(Instrux) == 8)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_REGISTER_IN_INSTRUCTION;
            }
            break;
        
        case ND_ILUT_FLT_DRLT8:
            // Debug register encoded in R must be less than 8.
            if (ND_DECODE_DR_R(Instrux) < 8)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_REGISTER_IN_INSTRUCTION;
            }
            break;

        case ND_ILUT_FLT_QRLT8:
            // Test register encoded in R must be less than 8.
            if (ND_DECODE_TR_R(Instrux) < 8)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_REGISTER_IN_INSTRUCTION;
            }
            break;

        case ND_ILUT_FLT_KRLT8:
            // Mask register encoded in R must be less than 8.
            if (ND_DECODE_MSK_R(Instrux) < 8)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_REGISTER_IN_INSTRUCTION;
            }
            break;

        case ND_ILUT_FLT_KVLT8:
            // Mask register encoded in V must be less than 8.
            if (ND_DECODE_MSK_V(Instrux) < 8)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_REGISTER_IN_INSTRUCTION;
            }
            break;

        case ND_ILUT_FLT_TRLT8:
            // Tile register encoded in R must be less than 8.
            if (ND_DECODE_TMM_R(Instrux) < 8)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_REGISTER_IN_INSTRUCTION;
            }
            break;

        case ND_ILUT_FLT_TMLT8:
            // Tile register encoded in M must be less than 8.
            if (ND_DECODE_TMM_M(Instrux) < 8 || Instrux->ModRm.mod != 3)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_REGISTER_IN_INSTRUCTION;
            }
            break;

        case ND_ILUT_FLT_TVLT8:
            // Tile register encoded in V must be less than 8.
            if (ND_DECODE_TMM_V(Instrux) < 8)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_REGISTER_IN_INSTRUCTION;
            }
            break;

        case ND_ILUT_FLT_VXNEVR_VXNEVV_VRNEVV:
            // Vector registers encoded in index, source & destination must be distinct.
            if (ND_DECODE_VEC_X(Instrux) != ND_DECODE_VEC_R(Instrux) &&
                ND_DECODE_VEC_X(Instrux) != ND_DECODE_VEC_V(Instrux) &&
                ND_DECODE_VEC_R(Instrux) != ND_DECODE_VEC_V(Instrux))
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_VSIB_REGS;
            }
            break;

        case ND_ILUT_FLT_VXNEVR:
            // Vector registers encoded in index & destination must be distinct.
            if (ND_DECODE_VEC_X(Instrux) != ND_DECODE_VEC_R(Instrux))
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_VSIB_REGS;
            }
            break;

        case ND_ILUT_FLT_TRNETM_TRNETV_TVNETM:
            // Tile registers encoded as destination, source 1 and source 2 must be distinct.
            if (ND_DECODE_TMM_R(Instrux) != ND_DECODE_TMM_M(Instrux) &&
                ND_DECODE_TMM_R(Instrux) != ND_DECODE_TMM_V(Instrux) &&
                ND_DECODE_TMM_V(Instrux) != ND_DECODE_TMM_M(Instrux))
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_TILE_REGS;
            }
            break;

        case ND_ILUT_FLT_VRNEVV_VRNEVM:
            // Vector registers encoded as destination & source must be distinct.
            if (ND_DECODE_VEC_R(Instrux) != ND_DECODE_VEC_V(Instrux) &&
                (ND_DECODE_VEC_R(Instrux) != ND_DECODE_VEC_M(Instrux) || Instrux->ModRm.mod != 3))
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_DEST_REGS;
            }
            break;

        case ND_ILUT_FLT_RVNE4_RMNE4:
            // GPR encoded in V, and GPR encoded in M must not be RSP.
            if (ND_DECODE_GPR_V(Instrux) != 4 && ND_DECODE_GPR_M(Instrux) != 4)
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_DEST_REGS;
            }
            break;

        case ND_ILUT_FLT_RVNERM:
            // GPR encoded in V must not be equal to GPR encoded in M.
            if (ND_DECODE_GPR_V(Instrux) != ND_DECODE_GPR_M(Instrux))
            {
                pTable = (const ND_TABLE *)pTable->Table[0];
            }
            else
            {
                return ND_STATUS_INVALID_DEST_REGS;
            }
            break;

        default:
            return ND_STATUS_INVALID_ENCODING;
        }
    }

    // No encoding found, or reached a non-instruction leaf - leave now.
    if (ND_NULL == pTable || pTable->Type != ND_ILUT_INSTRUCTION)
    {
        return ND_STATUS_INVALID_ENCODING;
    }

    Instrux->Idbe = ((ND_TABLE_INSTRUCTION *)pTable)->Index;

    Ictx->Idbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == Ictx->Idbe)
    {
        return ND_STATUS_INVALID_ENCODING;
    }

    *InsDef = Ictx->Idbe;
    
    Instrux->EvexMode = Ictx->Idbe->EvexMode;

    return ND_STATUS_SUCCESS;

#undef Instrux
#undef Context
}


//
// NdGetAddrAndOpMode
//
static NDSTATUS
NdGetAddrAndOpMode(
    const ND_INTERNAL_CONTEXT *Ictx
)
{
#define Context     Ictx->Context
#define Instrux     Ictx->Instrux
#define Idbe        Ictx->Idbe

    ND_BOOL w64, f64, d64, has66;

    if ((ND_CODE_64 != Instrux->DefCode) && !!(Idbe->Attributes & ND_FLAG_IWO64))
    {
        // Some instructions ignore VEX/EVEX.W field outside 64 bit mode, and treat it as 0.
        Instrux->Exs.w = 0;
    }

    // Extract the flags.
    w64 = (0 != Instrux->Exs.w) && !(Idbe->Attributes & ND_FLAG_WIG);

    // In 64 bit mode, the operand is forced to 64 bit. Size-changing prefixes are ignored.
    f64 = 0 != (Idbe->Attributes & ND_FLAG_F64) && (ND_VEND_AMD != Context->VendMode);

    // In 64 bit mode, the operand defaults to 64 bit. No 32 bit form of the instruction exists. Note that on AMD,
    // only default 64 bit operands exist, even for branches - no operand is forced to 64 bit.
    d64 = (0 != (Idbe->Attributes & ND_FLAG_D64)) ||
          (0 != (Idbe->Attributes & ND_FLAG_F64) && (ND_VEND_AMD == Context->VendMode));

    // Check if 0x66 is indeed interpreted as a size changing prefix. Note that if 0x66 is a mandatory prefix,
    // then it won't be interpreted as a size changing prefix. However, there is an exception: MOVBE and CRC32
    // have mandatory 0xF2, and 0x66 is in fact a size changing prefix.
    // For legacy instructions promoted to EVEX, in some cases, the compressed prefix pp has the same meaning
    // as the legacy 0x66 prefix.
    has66 = (Instrux->HasOpSize && (!Instrux->HasMandatory66 || (Idbe->Attributes & ND_FLAG_S66))) || 
            ((Instrux->Exs.p == 1) && (Idbe->Attributes & ND_FLAG_SCALABLE));

    // Fill in the effective operand size. Also validate instruction validity in given mode.
    switch (Instrux->DefCode)
    {
    case ND_CODE_16:
        Instrux->EfOpMode = has66 ? ND_OPSZ_32 : ND_OPSZ_16;
        break;
    case ND_CODE_32:
        Instrux->EfOpMode = has66 ? ND_OPSZ_16 : ND_OPSZ_32;
        break;
    case ND_CODE_64:
        Instrux->EfOpMode = (w64 || f64 || (d64 && !has66)) ? ND_OPSZ_64 : (has66 ? ND_OPSZ_16 : ND_OPSZ_32);
        Instrux->AddrMode = !!(Idbe->Attributes & ND_FLAG_I67) ? ND_ADDR_64 : Instrux->AddrMode;
        break;
    default:
        return ND_STATUS_INVALID_INSTRUX;
    }

    // Fill in the default word length. It can't be more than 8 bytes.
    Instrux->WordLength = 2 << Instrux->EfOpMode;

    return ND_STATUS_SUCCESS;

#undef Idbe
#undef Instrux
#undef Context
}


//
// NdGetVectorLength
//
static NDSTATUS
NdGetVectorLength(
    const ND_INTERNAL_CONTEXT *Ictx
)
{
#define Instrux     Ictx->Instrux

    if (Instrux->HasEr || Instrux->HasSae || Instrux->HasIgnEr)
    {
        // Embedded rounding or SAE present, force the vector length to 512 or scalar.
        if ((Ictx->Idbe->TupleType == ND_TUPLE_T1S) || 
            (Ictx->Idbe->TupleType == ND_TUPLE_T1S8) ||
            (Ictx->Idbe->TupleType == ND_TUPLE_T1S16) ||
            (Ictx->Idbe->TupleType == ND_TUPLE_T1F))
        {
            // Scalar instruction, vector length is 128 bits.
            Instrux->VecMode = Instrux->EfVecMode = ND_VECM_128;
        }
        else if (Instrux->Evex.u == 0)
        {
            // AVX 10 allows SAE/ER for 256-bit vector length, if EVEX.U is 0.
            // It is unclear whether the EVEX.U bit is ignored or reserved for scalar instructions.
            Instrux->VecMode = Instrux->EfVecMode = ND_VECM_256;
        }
        else
        {
            // Legacy or AVX 10 instruction with U bit set, vector length is 512 bits.
            Instrux->VecMode = Instrux->EfVecMode = ND_VECM_512;
        }

        return ND_STATUS_SUCCESS;
    }

    // Decode EVEX vector length. Also take into consideration the "ignore L" flag.
    switch (Instrux->Exs.l)
    {
    case 0:
        Instrux->VecMode = ND_VECM_128;
        Instrux->EfVecMode = ND_VECM_128;
        break;
    case 1:
        Instrux->VecMode = ND_VECM_256;
        Instrux->EfVecMode = (Ictx->Idbe->Attributes & ND_FLAG_LIG) ? ND_VECM_128 : ND_VECM_256;
        break;
    case 2:
        Instrux->VecMode = ND_VECM_512;
        Instrux->EfVecMode = (Ictx->Idbe->Attributes & ND_FLAG_LIG) ? ND_VECM_128 : ND_VECM_512;
        break;
    default:
        return ND_STATUS_BAD_EVEX_LL;
    }

    return ND_STATUS_SUCCESS;

#undef Instrux
}


//
// NdGetPrefixActivation
//
static NDSTATUS
NdGetPrefixActivation(
    const ND_INTERNAL_CONTEXT *Ictx
)
{
#define Context     Ictx->Context
#define Idbe        Ictx->Idbe
#define Instrux     Ictx->Instrux

    // These checks only apply to legacy encoded instructions.
    ND_VALID_PREFIXES validPrefixes;

    validPrefixes.Raw = Idbe->ValidPrefixes;

    // Check for LOCK. LOCK can be present only in two cases:
    // 1. For certain RMW instructions, as long as the destination operand is memory
    // 2. For MOV to/from CR0 in 32-bit mode on AMD CPUs, which allows access to CR8
    // For XOP/VEX/EVEX instructions, a #UD is generated (which is checked when fetching the XOP/VEX/EVEX prefix).
    if (Instrux->HasLock)
    {
        if (0 != (Idbe->Attributes & ND_FLAG_LOCK_SPECIAL) && (ND_CODE_32 == Instrux->DefCode))
        {
            // Special case of LOCK being used by MOV cr to access CR8.
        }
        else if (validPrefixes.Lock && (Instrux->ModRm.mod != 3))
        {
            // Lock is only present for instructions that are encoded using ModRM, and have memory destination,
            // so it is sufficient to check for memory mod in ModRM.
            Instrux->IsLockEnabled = 1;
        }
        else
        {
            return ND_STATUS_BAD_LOCK_PREFIX;
        }
    }

    // Check for REP prefixes. There are multiple uses:
    // 1. REP/REPNZ/REPZ, for string/IO instructions
    // 2. XACQUIRE/XRELEASE, for HLE-enabled instructions
    // 3. BND prefix, for branches
    // For XOP/VEX/EVEX instructions, a #UD is generated (which is checked when fetching the XOP/VEX/EVEX prefix).
    if (Instrux->Rep != 0)
    {
        Instrux->IsRepEnabled = validPrefixes.Rep != 0;

        Instrux->IsRepcEnabled = validPrefixes.RepCond != 0;

        // Bound enablement.
        Instrux->IsBndEnabled = (validPrefixes.Bnd != 0) && (Instrux->Rep == ND_PREFIX_G1_BND);

        // Check if the instruction is REPed.
        Instrux->IsRepeated = Instrux->IsRepEnabled || Instrux->IsRepcEnabled;

        // Check if the instruction is XACQUIRE or XRELEASE enabled.
        if ((Instrux->IsLockEnabled || validPrefixes.HleNoLock) && (Instrux->ModRm.mod != 3))
        {
            if ((validPrefixes.Xacquire || validPrefixes.Hle) && 
                (Instrux->Rep == ND_PREFIX_G1_XACQUIRE))
            {
                Instrux->IsXacquireEnabled = ND_TRUE;
            }
            else if ((validPrefixes.Xrelease || validPrefixes.Hle) && 
                (Instrux->Rep == ND_PREFIX_G1_XRELEASE))
            {
                Instrux->IsXreleaseEnabled = ND_TRUE;
            }
        }
    }

    // Check for segment prefixes. Besides offering segment override when accessing memory:
    // 1. Allow for branch hints to conditional branches
    // 2. Allow for Do Not Track prefix for indirect branches, to inhibit CET-IBT tracking
    // Segment prefixes are allowed with XOP/VEX/EVEX instructions, but they have the legacy meaning (no BHINT or DNT).
    if (Instrux->Seg != 0)
    {
        // Branch hint enablement.
        Instrux->IsBhintEnabled = validPrefixes.Bhint && (
            (Instrux->Seg == ND_PREFIX_G2_BR_TAKEN) ||
            (Instrux->Seg == ND_PREFIX_G2_BR_NOT_TAKEN) ||
            (Instrux->Seg == ND_PREFIX_G2_BR_ALT));

        // Do-not-track hint enablement.
        Instrux->IsDntEnabled = validPrefixes.Dnt && (Instrux->Seg == ND_PREFIX_G2_NO_TRACK);
    }

    // Check if the instruction is CET tracked. The do not track prefix (0x3E) works only for indirect near JMP and CALL
    // instructions. It is always enabled for far JMP and CALL instructions.
    Instrux->IsCetTracked = !!(Idbe->Attributes & ND_FLAG_CETT) && !Instrux->IsDntEnabled;

    return ND_STATUS_SUCCESS;

#undef Context
#undef Instrux
#undef Idbe
}


//
// NdGetDecoratorActivation
//
static NDSTATUS
NdGetDecoratorActivation(
    const ND_INTERNAL_CONTEXT *Ictx
)
{
#define Context     Ictx->Context
#define Idbe        Ictx->Idbe
#define Instrux     Ictx->Instrux

    // Mask for the 3rd byte of the EVEX prefix.
    static const ND_UINT8 b3mask[4] =
    {         // Bit              7     6     5     4     3     2     1     0
        0x00, // Regular form: |  z  |  L  |  L  |  b  |  V4 |  a  |  a  |  a  |
        0xD3, // VEX form:     |  0  |  0  |  L  |  0  |  V4 |  NF |  0  |  0  |
        0xE3, // Legacy form:  |  0  |  0  |  0  |  ND |  V4 |  NF |  0  |  0  |
        0xE0, // Cond form:    |  0  |  0  |  0  |  ND | SC3 | SC2 | SC1 | SC0 |
    };

    ND_VALID_DECORATORS validDecos;
    
    validDecos.Raw = Idbe->ValidDecorators;

    // Validate the EVEX prefix, depending on the EVEX extension mode.
    if (Instrux->EvexMode == ND_EVEXM_EVEX)
    {
        // EVEX.U is used as X4 (inverted) for index extension, when accessing memory.
        // EVEX.U is used as 256-bit SAE/ER semantic, when reg-reg form and b is 1.
        // Othweriwse, #UD.
        if (Instrux->Evex.u != 1 && Instrux->ModRm.mod == 3 && Instrux->Exs.bm == 0)
        {
            return ND_STATUS_BAD_EVEX_U;
        }

        // Handle embedded broadcast/rounding-control.
        if (Instrux->Exs.bm == 1)
        {
            if (Instrux->ModRm.mod == 3)
            {
                // reg form for the instruction, check for ER or SAE support.
                if (validDecos.Er)
                {
                    Instrux->HasEr = 1;
                    Instrux->HasSae = 1;
                    Instrux->RoundingMode = (ND_UINT8)Instrux->Exs.l;
                }
                else if (validDecos.Sae)
                {
                    Instrux->HasSae = 1;
                }
                else if (!!(Idbe->Attributes & ND_FLAG_IER))
                {
                    // The encoding behaves as if embedded rounding is enabled, but it is in fact ignored.
                    Instrux->HasIgnEr = 1;
                }
                else
                {
                    return ND_STATUS_ER_SAE_NOT_SUPPORTED;
                }
            }
            else
            {
                // mem form for the instruction, check for broadcast.
                if (validDecos.Broadcast)
                {
                    Instrux->HasBroadcast = 1;
                }
                else
                {
                    return ND_STATUS_BROADCAST_NOT_SUPPORTED;
                }
            }
        }

        // Handle masking.
        if (Instrux->Exs.k != 0)
        {
            if (validDecos.Mask)
            {
                Instrux->HasMask = 1;
            }
            else
            {
                return ND_STATUS_MASK_NOT_SUPPORTED;
            }
        }
        else
        {
            if (!!(Idbe->Attributes & ND_FLAG_MMASK))
            {
                return ND_STATUS_MASK_REQUIRED;
            }
        }

        // Handle zeroing.
        if (Instrux->Exs.z != 0)
        {
            if (validDecos.Zero)
            {
                // Zeroing restrictions:
                // - valid with register only;
                // - valid only if masking is also used;
                if (Instrux->HasMask)
                {
                    if (Instrux->ModRm.mod == 3 || (0 == (Idbe->Mem1Access & ND_ACCESS_ANY_WRITE)))
                    {
                        Instrux->HasZero = 1;
                    }
                    else
                    {
                        return ND_STATUS_ZEROING_ON_MEMORY;
                    }
                }
                else
                {
                    return ND_STATUS_ZEROING_NO_MASK;
                }
            }
            else
            {
                return ND_STATUS_ZEROING_NOT_SUPPORTED;
            }
        }

        // EVEX instructions with 8 bit displacement use compressed displacement addressing, where the displacement
        // is scaled according to the data type accessed by the instruction.
        Instrux->HasCompDisp = (ND_BOOL)(Instrux->HasDisp && Instrux->DispLength == 1);

        // ND, NF & ZU not valid for regular EVEX.
        Instrux->Exs.nd = 0;
        Instrux->Exs.nf = 0;
        Instrux->Exs.sc = 0;
    }
    else
    {
        // EVEX flavors are only valid in APX mode. Outside APX, only regular EVEX is valid.
        if (0 == (Context->FeatMode & ND_FEAT_APX))
        {
            return ND_STATUS_INVALID_ENCODING;
        }

        // Apply EVEX payload byte 3 mask.
        if (0 != (Instrux->Evex.Evex[3] & b3mask[Instrux->EvexMode]))
        {
            return ND_STATUS_INVALID_EVEX_BYTE3;
        }

        // EVEX.U field must be 1 if mod is reg-reg.
        if (Instrux->Evex.u == 0 && Instrux->ModRm.mod == 3)
        {
            return ND_STATUS_BAD_EVEX_U;
        }

        // ND, NF & ZU activation.
        Instrux->HasNd = (ND_BOOL)(Instrux->Exs.nd && validDecos.Nd);
        Instrux->HasNf = (ND_BOOL)(Instrux->Exs.nf && validDecos.Nf);
        Instrux->HasZu = (ND_BOOL)(Instrux->Exs.nd && validDecos.Zu);

        // Conditional instructions use Default Flags Value.
        Instrux->HasDfv = Instrux->EvexMode == ND_EVEXM_COND;

        // z, L'L, b & k valid only for regular EVEX.
        Instrux->Exs.z = 0;
        Instrux->Exs.l = 0;
        Instrux->Exs.bm = 0;
        Instrux->Exs.k = 0;
    }

    return ND_STATUS_SUCCESS;

#undef Instrux
#undef Idbe
#undef Context
}


//
// NdCopyInstructionInfo
//
static NDSTATUS
NdCopyInstructionInfo(
    INSTRUX *Instrux,
    const ND_IDBE *Idbe
)
{
    // Copy instruction database entry information.
    Instrux->Attributes = Idbe->Attributes;
    Instrux->Category = (ND_INS_CATEGORY)Idbe->Category;
    Instrux->IsaSet = (ND_INS_SET)Idbe->IsaSet;
    Instrux->FlagsAccess.Undefined.Raw = Idbe->SetFlags & Idbe->ClearedFlags;
    Instrux->FlagsAccess.Tested.Raw = Idbe->TestedFlags;
    Instrux->FlagsAccess.Modified.Raw = Idbe->ModifiedFlags;
    Instrux->FlagsAccess.Set.Raw = Idbe->SetFlags ^ Instrux->FlagsAccess.Undefined.Raw;
    Instrux->FlagsAccess.Cleared.Raw = Idbe->ClearedFlags ^ Instrux->FlagsAccess.Undefined.Raw;
    Instrux->CpuidFlag.Flag = Idbe->CpuidFlag;
    Instrux->ValidModes.Raw = Idbe->ValidModes;
    Instrux->ValidPrefixes.Raw = Idbe->ValidPrefixes;
    Instrux->ValidDecorators.Raw = Idbe->ValidDecorators;
    Instrux->FpuFlagsAccess.Raw = Idbe->FpuFlags;
    Instrux->SimdExceptions.Raw = Idbe->SimdExc;

    // Valid for EVEX, VEX and SSE instructions only. A value of 0 means it's not used.
    Instrux->ExceptionType = Idbe->ExcType;
    Instrux->TupleType = Idbe->TupleType;

    // Components access.
    Instrux->CsAccess = Idbe->CsAccess;
    Instrux->RipAccess = Idbe->RipAccess;
    Instrux->RflAccess = Idbe->RflAccess;
    Instrux->MemoryAccess = Idbe->Mem2Access | (Instrux->ModRm.mod != 3 ? Idbe->Mem1Access : 0);
    Instrux->StackAccess = Idbe->StkAccess;
    Instrux->StackWords = Idbe->StkWords;

    // Decoded branch information.
    if (!!(Instrux->RipAccess & ND_ACCESS_ANY_WRITE))
    {
        Instrux->BranchInfo.IsBranch = 1;
        Instrux->BranchInfo.IsConditional = !!(Instrux->RipAccess & ND_ACCESS_COND_WRITE);
        Instrux->BranchInfo.IsFar = Instrux->CsAccess != 0;
        Instrux->BranchInfo.IsIndirect = Instrux->HasModRm ? 1 : 0;
    }

#ifndef BDDISASM_NO_MNEMONIC
    Instrux->Mnemonic = NdIdbeGetMnemonic(Idbe->Mnemonic);
#endif // !BDDISASM_NO_MNEMONIC

    return ND_STATUS_SUCCESS;
}


//
// NdDecodeInternal
//
static NDSTATUS
NdDecodeInternal(
    ND_INTERNAL_CONTEXT *Ictx,
    const ND_UINT8 *Code,
    ND_SIZET Size
)
{
    NDSTATUS status = ND_STATUS_SUCCESS;
    PND_IDBE pIns = ND_NULL;

    Ictx->Instrux->DefCode = (ND_UINT8)Ictx->Context->DefCode;
    Ictx->Instrux->DefData = (ND_UINT8)Ictx->Context->DefData;
    Ictx->Instrux->DefStack = (ND_UINT8)Ictx->Context->DefStack;

    // Initialize default address & operand size.
    Ictx->Instrux->AddrMode = (ND_UINT8)Ictx->Context->DefCode;
    Ictx->Instrux->OpMode = Ictx->Context->DefCode == ND_CODE_16 ? ND_OPSZ_16 : ND_OPSZ_32;

    // Fetch prefixes.
    status = NdFetchPrefixes(Ictx, Code, 0, Size);
    if (!ND_SUCCESS(status))
    {
        return status;
    }

    // Fetch opcodes.
    status = NdFetchOpcodes(Ictx, Code, Ictx->Instrux->Length, Size);
    if (!ND_SUCCESS(status))
    {
        return status;
    }

    // Fetch ModRm, SIB & Displacement.
    status = NdFetchModrmSibDisplacement(Ictx, Code, Ictx->Instrux->Length, Size);
    if (!ND_SUCCESS(status))
    {
        return status;
    }

    // Start iterating the tables, in order to extract the instruction entry. Most of the
    // encoding validity checks are performed during this step (i.e., an invalid encoding would
    // lead through a path in the decoding tables that results in a NULL entry).
    status = NdDecodeInstruction(Ictx, Code, Size, &pIns);
    if (!ND_SUCCESS(status))
    {
        return status;
    }

    // Get effective operand mode.
    status = NdGetAddrAndOpMode(Ictx);
    if (!ND_SUCCESS(status))
    {
        return status;
    }

    if (0 != pIns->Ipb)
    {
        // Fetch instruction payload bytes (immediates, relative offsets, etc.).
        status = NdFetchPayloadBytes(Ictx, Code, Ictx->Instrux->Length, Size);
        if (!ND_SUCCESS(status))
        {
            return status;
        }
    }

    if (ND_ENCM_LEGACY == Ictx->Instrux->EncMode)
    {
        // Check prefix activation. Only available for legacy instructions. For XOP/VEX/EVEX instructions:
        // 1. LOCK, REP, 0x66, REX, REX2 cause #UD (checkd during XOP/VEX/EVEX fetch)
        // 2. Segment prefixes do not have BHINT or DNT semantic
        // 3. 0x67 can be used to override address mode
        status = NdGetPrefixActivation(Ictx);
        if (!ND_SUCCESS(status))
        {
            return status;
        }
    }
    else if (ND_ENCM_EVEX == Ictx->Instrux->EncMode)
    {
        // Post-process EVEX encoded instructions. This does two thing:
        // - check and fill in decorator info;
        // - generate error for invalid broadcast/rounding, mask or zeroing bits;
        // - generate error if any reserved bits are set.
        status = NdGetDecoratorActivation(Ictx);
        if (!ND_SUCCESS(status))
        {
            return status;
        }
    }

    if (pIns->Attributes & ND_FLAG_VECTOR)
    {
        // Get vector length.
        status = NdGetVectorLength(Ictx);
        if (!ND_SUCCESS(status))
        {
            return status;
        }
    }
    
    // Fill in operands count.
    Ictx->Instrux->ExpOperandsCount = ND_EXP_OPS_CNT(pIns->OpsCount);
    Ictx->Instrux->OperandsCount = Ictx->Instrux->ExpOperandsCount;
    
    if (!(Ictx->Context->Options & ND_OPTION_ONLY_EXPLICIT_OPERANDS))
    {
        Ictx->Instrux->OperandsCount += ND_IMP_OPS_CNT(pIns->OpsCount);
    }

    Ictx->Instrux->Instruction = (ND_INS_CLASS)pIns->Instruction;

    // All done! Instruction successfully decoded!
    return ND_STATUS_SUCCESS;
}


NDSTATUS
NdDecodeWithContext(
    INSTRUX *Instrux,
    const ND_UINT8 *Code,
    ND_SIZET Size,
    ND_CONTEXT *Context
)
{
    ND_INTERNAL_CONTEXT iCtx;
    NDSTATUS status = ND_STATUS_SUCCESS;
    ND_UINT32 opIndex;

    // validate
    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (ND_NULL == Code)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (Size == 0)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (ND_NULL == Context)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (ND_CODE_64 < Context->DefCode)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (ND_STACK_64 < Context->DefStack)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (ND_VEND_MAX < Context->VendMode)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (0 == (Context->Options & ND_OPTION_SKIP_ZERO_INSTRUX))
    {
        // Initialize with zero.
        nd_memzero(Instrux, sizeof(INSTRUX));
    }

    // Fetch the instruction bytes. Note than in regular decode mode, ND_OPT_SKIP_CACHE_IBYTES is ignored.
    for (opIndex = 0; 
         opIndex < ((Size < ND_MAX_INSTRUCTION_LENGTH) ? Size : ND_MAX_INSTRUCTION_LENGTH); 
         opIndex++)
    {
        Instrux->InstructionBytes[opIndex] = Code[opIndex];
    }

    // Initialize the internal context.
    iCtx.Context = Context;
    iCtx.Instrux = (INSTRUX_MINI*)Instrux;
    iCtx.Idbe = ND_NULL;

    // Decode the instruction.
    status = NdDecodeInternal(&iCtx, Instrux->InstructionBytes, Size);
    if (!ND_SUCCESS(status))
    {
        return status;
    }

    if (iCtx.Idbe == ND_NULL)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }

    // Copy information inside the Instrux.
    status = NdCopyInstructionInfo(Instrux, iCtx.Idbe);
    if (!ND_SUCCESS(status))
    {
        return status;
    }

    // Copy opcode bytes.
    for (opIndex = 0; opIndex < Instrux->OpLength; opIndex++)
    {
        Instrux->OpCodeBytes[opIndex] = Instrux->InstructionBytes[Instrux->OpOffset + opIndex];
    }

    // Copy legacy information.
    Instrux->FeatMode = (ND_UINT8)Context->FeatMode;
    Instrux->VendMode = (ND_UINT8)Context->VendMode;

    // And now decode each operand.
    for (opIndex = 0; opIndex < Instrux->OperandsCount; ++opIndex)
    {
        status = NdParseOperand((INSTRUX_MINI *)Instrux, iCtx.Idbe,
                                &Instrux->Operands[opIndex],
                                iCtx.Idbe->Operands[opIndex]);
        if (!ND_SUCCESS(status))
        {
            return status;
        }
    }

    // All done! Instruction successfully decoded!
    return ND_STATUS_SUCCESS;
}


NDSTATUS
NdDecodeWithContextMini(
    INSTRUX_MINI *Instrux,
    const ND_UINT8 *Code,
    ND_SIZET Size,
    ND_CONTEXT *Context
)
{
    ND_INTERNAL_CONTEXT iCtx;
    ND_SIZET toFetch;
    ND_UINT8 iBytes[ND_MAX_INSTRUCTION_LENGTH];
    const ND_UINT8 *iCode = ND_NULL;

    // validate
    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (ND_NULL == Code)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (Size == 0)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (ND_NULL == Context)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (ND_CODE_64 < Context->DefCode)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (ND_STACK_64 < Context->DefStack)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (ND_VEND_MAX < Context->VendMode)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (0 == (Context->Options & ND_OPTION_SKIP_ZERO_INSTRUX))
    {
        // Initialize with zero.
        nd_memzero(Instrux, sizeof(INSTRUX_MINI));
    }

    if (0 == (Context->Options & ND_OPTION_SKIP_CACHE_IBYTES))
    {
        toFetch = (Size < ND_MAX_INSTRUCTION_LENGTH) ? Size : ND_MAX_INSTRUCTION_LENGTH;

        // Fetch the instruction bytes. Note than in regular decode mode, ND_OPT_SKIP_CACHE_IBYTES is ignored.
        for (ND_SIZET i = 0; i < toFetch; i++)
        {
            iBytes[i] = Code[i];
        }

        iCode = iBytes;
    }
    else
    {
        toFetch = Size;
        iCode = Code;
    }

    // Initialize the internal context.
    iCtx.Context = Context;
    iCtx.Instrux = (INSTRUX_MINI*)Instrux;
    iCtx.Idbe = ND_NULL;

    // Decode the instruction.
    return NdDecodeInternal(&iCtx, iCode, toFetch);
}


//
// NdDecodeEx2
//
NDSTATUS
NdDecodeEx2(
    INSTRUX *Instrux,
    const ND_UINT8 *Code,
    ND_SIZET Size,
    ND_UINT8 DefCode,
    ND_UINT8 DefData,
    ND_UINT8 DefStack,
    ND_UINT8 Vendor
)
{
    ND_CONTEXT opt;

    NdInitContext(&opt);

    opt.DefCode = DefCode;
    opt.DefData = DefData;
    opt.DefStack = DefStack;
    opt.VendMode = Vendor;
    opt.FeatMode = ND_FEAT_ALL; // Optimistically decode everything, as if all features are enabled.

    return NdDecodeWithContext(Instrux, Code, Size, &opt);
}


//
// NdDecodeEx
//
NDSTATUS
NdDecodeEx(
    INSTRUX *Instrux,
    const ND_UINT8 *Code,
    ND_SIZET Size,
    ND_UINT8 DefCode,
    ND_UINT8 DefData
)
{
    return NdDecodeEx2(Instrux, Code, Size, DefCode, DefData, DefCode, ND_VEND_ANY);
}


//
// NdDecode
//
NDSTATUS
NdDecode(
    INSTRUX *Instrux,
    const ND_UINT8 *Code,
    ND_UINT8 DefCode,
    ND_UINT8 DefData
)
{
    return NdDecodeEx2(Instrux, Code, ND_MAX_INSTRUCTION_LENGTH, DefCode, DefData, DefCode, ND_VEND_ANY);
}

NDSTATUS
NdDecodeMini(
    INSTRUX_MINI *Instrux,      // Output decoded instruction.
    const ND_UINT8 *Code,       // Buffer containing the instruction bytes.
    ND_SIZET Size,              // Maximum size of the Code buffer.
    ND_UINT8 DefCode            // Decode mode - one of the ND_CODE_* values.
)
{
    ND_CONTEXT opt;

    NdInitContext(&opt);

    opt.DefCode = DefCode;
    opt.DefData = DefCode;
    opt.DefStack = DefCode;
    opt.VendMode = ND_VEND_ANY;
    opt.FeatMode = ND_FEAT_ALL; // Optimistically decode everything, as if all features are enabled.

    return NdDecodeWithContextMini(Instrux, Code, Size, &opt);
}


//
// NdInitContext
//
void
NdInitContext(
    ND_CONTEXT *Context
)
{
    nd_memzero(Context, sizeof(*Context));
}
