/*
 * Copyright (c) 2020 Bitdefender
 * SPDX-License-Identifier: Apache-2.0
 */
#include "include/bddisasm_crt.h"
#include "../inc/bddisasm.h"


#if !defined(BDDISASM_NO_MNEMONIC) && !defined(BDDISASM_NO_FORMAT)

static const char *const gReg8Bit[] =
{
    "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh",
};

static const char *const gReg8Bit64[] =
{
    "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil",
    "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b",
    "r16b", "r17b", "r18b", "r19b", "r20b", "r21b", "r22b", "r23b",
    "r24b", "r25b", "r26b", "r27b", "r28b", "r29b", "r30b", "r31b",
};

static const char *const gReg16Bit[] =
{
    "ax", "cx", "dx", "bx", "sp", "bp", "si", "di",
    "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w",
    "r16w", "r17w", "r18w", "r19w", "r20w", "r21w", "r22w", "r23w",
    "r24w", "r25w", "r26w", "r27w", "r28w", "r29w", "r30w", "r31w",
};

static const char *const gReg32Bit[] =
{
    "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi",
    "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d",
    "r16d", "r17d", "r18d", "r19d", "r20d", "r21d", "r22d", "r23d",
    "r24d", "r25d", "r26d", "r27d", "r28d", "r29d", "r30d", "r31d",
};

static const char *const gReg64Bit[] =
{
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
};

static const char *const gRegFpu[] =
{
    "st0", "st1", "st2", "st3", "st4", "st5", "st6", "st7",
};

static const char *const gRegMmx[] =
{
    "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm7",
};

static const char *const gRegControl[] =
{
    "cr0", "cr1", "cr2", "cr3", "cr4", "cr5", "cr6", "cr7",
    "cr8", "cr9", "cr10", "cr11", "cr12", "cr13", "cr14", "cr15",
    "cr16", "cr17", "cr18", "cr19", "cr20", "cr21", "cr22", "cr23",
    "cr24", "cr25", "cr26", "cr27", "cr28", "cr29", "cr30", "cr31",
};

static const char *const gRegDebug[] =
{
    "dr0", "dr1", "dr2", "dr3", "dr4", "dr5", "dr6", "dr7",
    "dr8", "dr9", "dr10", "dr11", "dr12", "dr13", "dr14", "dr15",
    "dr16", "dr17", "dr18", "dr19", "dr20", "dr21", "dr22", "dr23",
    "dr24", "dr25", "dr26", "dr27", "dr28", "dr29", "dr30", "dr31",
};

static const char *const gRegTest[] =
{
    "tr0", "tr1", "tr2", "tr3", "tr4", "tr5", "tr6", "tr7",
    "tr8", "tr9", "tr10", "tr11", "tr12", "tr13", "tr14", "tr15",
};

static const char *const gRegXmm[] =
{
    "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
    "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",
    "xmm16", "xmm17", "xmm18", "xmm19", "xmm20", "xmm21", "xmm22", "xmm23",
    "xmm24", "xmm25", "xmm26", "xmm27", "xmm28", "xmm29", "xmm30", "xmm31",
};

static const char *const gRegYmm[] =
{
    "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "ymm5", "ymm6", "ymm7",
    "ymm8", "ymm9", "ymm10", "ymm11", "ymm12", "ymm13", "ymm14", "ymm15",
    "ymm16", "ymm17", "ymm18", "ymm19", "ymm20", "ymm21", "ymm22", "ymm23",
    "ymm24", "ymm25", "ymm26", "ymm27", "ymm28", "ymm29", "ymm30", "ymm31"
};

static const char *const gRegZmm[] =
{
    "zmm0", "zmm1", "zmm2", "zmm3", "zmm4", "zmm5", "zmm6", "zmm7",
    "zmm8", "zmm9", "zmm10", "zmm11", "zmm12", "zmm13", "zmm14", "zmm15",
    "zmm16", "zmm17", "zmm18", "zmm19", "zmm20", "zmm21", "zmm22", "zmm23",
    "zmm24", "zmm25", "zmm26", "zmm27", "zmm28", "zmm29", "zmm30", "zmm31",
};

static const char *const gRegSeg[] =
{
    "es", "cs", "ss", "ds", "fs", "gs", "segr6", "segr7",
};

static const char *const gRegBound[] =
{
    "bnd0", "bnd1", "bnd2", "bnd3",
};

static const char *const gRegMask[] =
{
    "k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7",
};

static const char *const gRegTile[] =
{
    "tmm0", "tmm1", "tmm2", "tmm3", "tmm4", "tmm5", "tmm6", "tmm7",
};

static const char *const gEmbeddedRounding[] =
{
    "rn", "rd", "ru", "rz",
};


//
// NdSprintf
//
static NDSTATUS
NdSprintf(
    char *Destination,
    ND_SIZET DestinationSize,
    const char *Formatstring,
    ...
)
//
// Wrapper on vsnprintf.
//
{
    int res;
    va_list args;

    if (ND_NULL == Destination)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (ND_NULL == Formatstring)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    nd_memzero(Destination, DestinationSize);

    va_start(args, Formatstring);

    // _vsnprintf is used instead of the more secure _vsnprintf_s because the mini-Petru supports just
    // the unsecured version, and we depend on it.
    res = nd_vsnprintf_s(Destination, DestinationSize, DestinationSize - 1, Formatstring, args);

    va_end(args);

    if ((res < 0) || ((ND_SIZET)res >= DestinationSize - 1))
    {
        return ND_STATUS_BUFFER_OVERFLOW;
    }

    return ND_STATUS_SUCCESS;
}


//
// NdToTextInternal
//
static NDSTATUS
NdToTextInternal(
    const INSTRUX_MINI *Instrux,
    const ND_OPERAND *Operands,
    ND_UINT64 Rip,
    ND_UINT32 BufferSize,
    char *Buffer
)
{
    NDSTATUS status;
    char *res, temp[64];
    ND_UINT32 opsStored;
    ND_OPERAND localOp;
    const ND_OPERAND *pOp;
    ND_BOOL alignmentStored;

#define NDTOTEXT_APPEND(str)                         \
{                                                    \
    res = nd_strcat_s(Buffer, BufferSize, (str));    \
    RET_EQ(res, ND_NULL, ND_STATUS_BUFFER_OVERFLOW); \
}

    // pre-init
    status = ND_STATUS_SUCCESS;
    res = (char *)ND_NULL;
    opsStored = 0;
    pOp = (const ND_OPERAND *)ND_NULL;
    alignmentStored = ND_FALSE;

    // Validate args.
    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (ND_NULL == Buffer)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (BufferSize < ND_MIN_BUF_SIZE)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    // init the text.
    nd_memzero(Buffer, BufferSize);
    nd_memzero(temp, sizeof(temp));

    // First, store the prefixes.

    // Check for REPZ/REPNZ support, and store prefixes.
    if (Instrux->IsRepcEnabled)
    {
        if (Instrux->Rep == ND_PREFIX_G1_REPE_REPZ)
        {
            NDTOTEXT_APPEND("REPZ ");
        }
        else if (Instrux->Rep == ND_PREFIX_G1_REPNE_REPNZ)
        {
            NDTOTEXT_APPEND("REPNZ ");
        }
    }

    // Check for REP support and store prefixes.
    if (Instrux->IsRepEnabled)
    {
        if (Instrux->Rep == ND_PREFIX_G1_REPE_REPZ)
        {
            NDTOTEXT_APPEND("REP ");
        }
        else if (Instrux->Rep == ND_PREFIX_G1_REPNE_REPNZ)
        {
            NDTOTEXT_APPEND("REPNZ ");
        }
    }

    if (Instrux->IsXreleaseEnabled)
    {
        NDTOTEXT_APPEND("XRELEASE ");
    }
    else if (Instrux->IsXacquireEnabled)
    {
        NDTOTEXT_APPEND("XACQUIRE ");
    }

    if (Instrux->IsLockEnabled)
    {
        NDTOTEXT_APPEND("LOCK ");
    }

    if (Instrux->IsBndEnabled)
    {
        NDTOTEXT_APPEND("BND ");
    }

    if (Instrux->IsBhintEnabled)
    {
        switch (Instrux->Seg)
        {
        case ND_PREFIX_G2_BR_TAKEN:
            NDTOTEXT_APPEND("BHT ");
            break;

        case ND_PREFIX_G2_BR_NOT_TAKEN:
            NDTOTEXT_APPEND("BHNT ");
            break;

        case ND_PREFIX_G2_BR_ALT:
            NDTOTEXT_APPEND("BHALT ");
            break;

        default:
            break;
        }
    }

    if (Instrux->IsDntEnabled)
    {
        NDTOTEXT_APPEND("DNT ");
    }

    // Store the mnemonic.
    NDTOTEXT_APPEND(NdGetMnemonicMini(Instrux));

    // If there are no explicit operands, we can leave.
    if (0 == Instrux->ExpOperandsCount)
    {
        return ND_STATUS_SUCCESS;
    }

    // Check for default flags value.
    if (Instrux->HasDfv)
    {
        ND_BOOL comma = ND_FALSE;

        NDTOTEXT_APPEND(" {dfv=");

        if (Instrux->EvexCond.OF)
        {
            NDTOTEXT_APPEND("OF");
            comma = ND_TRUE;
        }

        if (Instrux->EvexCond.SF)
        {
            NDTOTEXT_APPEND(comma ? ",SF" : "SF");
            comma = ND_TRUE;
        }

        if (Instrux->EvexCond.ZF)
        {
            NDTOTEXT_APPEND(comma ? ",ZF" : "ZF");
            comma = ND_TRUE;
        }

        if (Instrux->EvexCond.CF)
        {
            NDTOTEXT_APPEND(comma ? ",CF" : "CF");
        }

        NDTOTEXT_APPEND("}");
    }

    // Now the operands.
    for (ND_UINT8 opIndex = 0; opIndex < Instrux->ExpOperandsCount; opIndex++)
    {
        status = ND_STATUS_SUCCESS;

        if (Operands != ND_NULL)
        {
            pOp = &Operands[opIndex];
        }
        else
        {
            status = NdGetOperandMini(Instrux, opIndex, &localOp);
            if (!ND_SUCCESS(status))
            {
                return status;
            }

            pOp = &localOp;
        }

        if (pOp->Type == ND_OP_NOT_PRESENT)
        {
            break;
        }

        if (pOp->Flags.IsDefault)
        {
            continue;
        }

        if (pOp->Type == ND_OP_DFV)
        {
            continue;
        }

        // If this is a mask operand that has been used as masking for a previous operand, than we
        // can safely skip it. We check this by seeing where is the operand encoded. If it's encoded
        // in the evex.aaa field, than it is a conventional mask.
        if ((pOp->Encoding == ND_OPE_A) && (pOp->Type == ND_OP_REG) &&
            (pOp->Info.Register.Type == ND_REG_MSK) && (opIndex > 0))
        {
            continue;
        }

        // Store alignment.
        if (!alignmentStored)
        {
            ND_SIZET idx = 0;

            while ((idx < BufferSize) && (Buffer[idx]))
            {
                idx++;
            }

            while ((idx < 9) && (idx + 1 < BufferSize))
            {
                Buffer[idx++] = 0x20;
            }

            if (idx + 1 < BufferSize)
            {
                Buffer[idx++] = 0x20;
            }

            Buffer[idx] = 0;

            alignmentStored = ND_TRUE;
        }

        // Store the comma, if this isn't the first operand.
        if (opsStored > 0)
        {
            NDTOTEXT_APPEND(", ");
        }

        opsStored++;

        switch (pOp->Type)
        {
        case ND_OP_REG:
            switch (pOp->Info.Register.Type)
            {
            case ND_REG_GPR:
            {
                if (pOp->Info.Register.Reg >= ND_MAX_GPR_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                // General purpose register.
                switch (pOp->Info.Register.Size)
                {
                case ND_SIZE_8BIT:
                    // 8 bit register.
                    if ((Instrux->EncMode != ND_ENCM_LEGACY) || Instrux->HasRex || Instrux->HasRex2)
                    {
                        NDTOTEXT_APPEND(gReg8Bit64[pOp->Info.Register.Reg]);
                    }
                    else
                    {
                        NDTOTEXT_APPEND(gReg8Bit[pOp->Info.Register.Reg]);
                    }
                    break;

                case ND_SIZE_16BIT:
                    // 16 bit register.
                    NDTOTEXT_APPEND(gReg16Bit[pOp->Info.Register.Reg]);
                    break;

                case ND_SIZE_32BIT:
                    // 32 bit register.
                    NDTOTEXT_APPEND(gReg32Bit[pOp->Info.Register.Reg]);
                    break;

                case ND_SIZE_64BIT:
                    // 64 bit register.
                    NDTOTEXT_APPEND(gReg64Bit[pOp->Info.Register.Reg]);
                    break;

                default:
                    return ND_STATUS_INVALID_INSTRUX;
                }
            }
            break;

            case ND_REG_SEG:
            {
                if (pOp->Info.Register.Reg >= ND_MAX_SEG_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                NDTOTEXT_APPEND(gRegSeg[pOp->Info.Register.Reg]);
            }
            break;

            case ND_REG_FPU:
            {
                if (pOp->Info.Register.Reg >= ND_MAX_FPU_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                NDTOTEXT_APPEND(gRegFpu[pOp->Info.Register.Reg]);
            }
            break;

            case ND_REG_MMX:
            {
                if (pOp->Info.Register.Reg >= ND_MAX_MMX_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                NDTOTEXT_APPEND(gRegMmx[pOp->Info.Register.Reg]);
            }
            break;

            case ND_REG_SSE:
            {
                if (pOp->Info.Register.Reg >= ND_MAX_SSE_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                switch (pOp->Info.Register.Size)
                {
                case ND_SIZE_128BIT:
                    NDTOTEXT_APPEND(gRegXmm[pOp->Info.Register.Reg]);
                    break;
                case ND_SIZE_256BIT:
                    NDTOTEXT_APPEND(gRegYmm[pOp->Info.Register.Reg]);
                    break;
                case ND_SIZE_512BIT:
                    NDTOTEXT_APPEND(gRegZmm[pOp->Info.Register.Reg]);
                    break;
                default:
                    return ND_STATUS_INVALID_INSTRUX;
                }
            }
            break;

            case ND_REG_CR:
            {
                if (pOp->Info.Register.Reg >= ND_MAX_CR_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                NDTOTEXT_APPEND(gRegControl[pOp->Info.Register.Reg]);
            }
            break;

            case ND_REG_DR:
            {
                if (pOp->Info.Register.Reg >= ND_MAX_DR_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                NDTOTEXT_APPEND(gRegDebug[pOp->Info.Register.Reg]);
            }
            break;

            case ND_REG_TR:
            {
                if (pOp->Info.Register.Reg >= ND_MAX_TR_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                NDTOTEXT_APPEND(gRegTest[pOp->Info.Register.Reg]);
            }
            break;

            case ND_REG_BND:
            {
                // Sanity check.
                if (pOp->Info.Register.Reg >= ND_MAX_BND_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                NDTOTEXT_APPEND(gRegBound[pOp->Info.Register.Reg]);
            }
            break;

            case ND_REG_MSK:
            {
                // Sanity check.
                if (pOp->Info.Register.Reg >= ND_MAX_MSK_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                NDTOTEXT_APPEND(gRegMask[pOp->Info.Register.Reg]);
            }
            break;

            case ND_REG_TILE:
            {
                // Sanity check.
                if (pOp->Info.Register.Reg >= ND_MAX_TILE_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                NDTOTEXT_APPEND(gRegTile[pOp->Info.Register.Reg]);
            }
            break;

            default:
                break;
            }

            if (pOp->Info.Register.Count > 1)
            {
                status = NdSprintf(temp, sizeof(temp), "+%d", pOp->Info.Register.Count - 1);
                if (!ND_SUCCESS(status))
                {
                    return status;
                }

                NDTOTEXT_APPEND(temp);
            }

            break;

        case ND_OP_BANK:
            // Nothing to show.
            break;

        case ND_OP_CONST:
        {
            // Implicit constant
            status = NdSprintf(temp, sizeof(temp), "%d", pOp->Info.Constant.Const);
            if (!ND_SUCCESS(status))
            {
                return status;
            }

            NDTOTEXT_APPEND(temp);
        }
        break;

        case ND_OP_IMM:
        {
            switch (pOp->Size)
            {
            case 1:
                status = NdSprintf(temp, sizeof(temp), "0x%02x", (ND_UINT8)pOp->Info.Immediate.Imm);
                break;
            case 2:
                status = NdSprintf(temp, sizeof(temp), "0x%04x", (ND_UINT16)pOp->Info.Immediate.Imm);
                break;
            case 4:
                status = NdSprintf(temp, sizeof(temp), "0x%08x", (ND_UINT32)pOp->Info.Immediate.Imm);
                break;
            case 8:
                status = NdSprintf(temp, sizeof(temp), "0x%016llx", (ND_UINT64)pOp->Info.Immediate.Imm);
                break;
            }
            if (!ND_SUCCESS(status))
            {
                return status;
            }

            NDTOTEXT_APPEND(temp);
        }
        break;

        case ND_OP_OFFS:
        {
            ND_UINT64 dest = Rip + Instrux->Length + pOp->Info.RelativeOffset.Rel;

            // Truncate to the actual word length.
            switch (Instrux->WordLength)
            {
            case 2:
                dest &= 0xFFFF;
                break;
            case 4:
                dest &= 0xFFFFFFFF;
                break;
            default:
                break;
            }

            status = NdSprintf(temp, sizeof(temp), "0x%llx", dest);
            if (!ND_SUCCESS(status))
            {
                return status;
            }

            NDTOTEXT_APPEND(temp);
        }
        break;

        case ND_OP_ADDR_FAR:
        {
            switch (Instrux->AddrLength)
            {
            case 4:
                status = NdSprintf(temp, sizeof(temp), "0x%04x:0x%04x",
                    pOp->Info.Address.BaseSeg, (ND_UINT16)pOp->Info.Address.Offset);
                break;
            case 6:
                status = NdSprintf(temp, sizeof(temp), "0x%04x:0x%08x",
                    pOp->Info.Address.BaseSeg, (ND_UINT32)pOp->Info.Address.Offset);
                break;
            case 10:
                status = NdSprintf(temp, sizeof(temp), "0x%04x:0x%016llx",
                    pOp->Info.Address.BaseSeg, (ND_UINT64)pOp->Info.Address.Offset);
                break;
            default:
                return ND_STATUS_INVALID_INSTRUX;
            }

            if (!ND_SUCCESS(status))
            {
                return status;
            }

            NDTOTEXT_APPEND(temp);
        }
        break;

        case ND_OP_ADDR_NEAR:
        {
            status = NdSprintf(temp, sizeof(temp), "0x%016llx", pOp->Info.AddressNear.Target);
            if (!ND_SUCCESS(status))
            {
                return status;
            }

            NDTOTEXT_APPEND(temp);
        }
        break;

        case ND_OP_MEM:
        {
            // Prepend the size. For VSIB addressing, store the VSIB element size, not the total accessed size.
            ND_OPERAND_SIZE size = pOp->Info.Memory.IsVsib ? pOp->Info.Memory.Vsib.ElemSize : pOp->Size;

            switch (size)
            {
            case 1:
                NDTOTEXT_APPEND("byte ptr ");
                break;
            case 2:
                NDTOTEXT_APPEND("word ptr ");
                break;
            case 4:
                NDTOTEXT_APPEND("dword ptr ");
                break;
            case 6:
                NDTOTEXT_APPEND("fword ptr ");
                break;
            case 8:
                NDTOTEXT_APPEND("qword ptr ");
                break;
            case 10:
                NDTOTEXT_APPEND("tbyte ptr ");
                break;
            case 16:
                NDTOTEXT_APPEND("xmmword ptr ");
                break;
            case 32:
                NDTOTEXT_APPEND("ymmword ptr ");
                break;
            case 48:
                NDTOTEXT_APPEND("m384 ptr ");
                break;
            case 64:
                NDTOTEXT_APPEND("zmmword ptr ");
                break;
            default:
                break;
            }

            // Prepend the segment, only if it is overridden via a prefix.
            if (pOp->Info.Memory.HasSeg && Instrux->HasSeg)
            {
                // No need for Seg range checks, as it is limited by the data type representation - 3 bits.
                //if (pOp->Info.Memory.Seg >= ND_MAX_SEG_REGS)
                //{
                //    return ND_STATUS_INVALID_INSTRUX;
                //}

                if ((ND_CODE_64 != Instrux->DefCode) || (NDR_FS == pOp->Info.Memory.Seg) ||
                    (NDR_GS == pOp->Info.Memory.Seg))
                {
                    NDTOTEXT_APPEND(gRegSeg[pOp->Info.Memory.Seg]);

                    NDTOTEXT_APPEND(":");
                }
            }

            // Prepend the "["
            NDTOTEXT_APPEND("[");

            // Base, if any.
            if (pOp->Info.Memory.HasBase)
            {
                // No need for Base range checks, as it is limited by the data type representation - 5 bits.
                //if (pOp->Info.Memory.Base >= ND_MAX_GPR_REGS)
                //{
                //    return ND_STATUS_INVALID_INSTRUX;
                //}

                switch (pOp->Info.Memory.BaseSize)
                {
                case ND_SIZE_8BIT:
                    if ((Instrux->EncMode != ND_ENCM_LEGACY) || Instrux->HasRex || Instrux->HasRex2)
                    {
                        NDTOTEXT_APPEND(gReg8Bit64[pOp->Info.Memory.Base]);
                    }
                    else
                    {
                        NDTOTEXT_APPEND(gReg8Bit[pOp->Info.Memory.Base]);
                    }
                    break;
                case ND_SIZE_16BIT:
                    NDTOTEXT_APPEND(gReg16Bit[pOp->Info.Memory.Base]);
                    break;
                case ND_SIZE_32BIT:
                    NDTOTEXT_APPEND(gReg32Bit[pOp->Info.Memory.Base]);
                    break;
                case ND_SIZE_64BIT:
                    NDTOTEXT_APPEND(gReg64Bit[pOp->Info.Memory.Base]);
                    break;
                default:
                    return ND_STATUS_INVALID_INSTRUX;
                }
            }

            // Index, if any. Special treatment for VSIB addressing. Also, prepend a "+" if base is present.
            if (pOp->Info.Memory.HasIndex)
            {
                if (pOp->Info.Memory.Index >= (pOp->Info.Memory.IsVsib ? ND_MAX_SSE_REGS : ND_MAX_GPR_REGS))
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                if (pOp->Info.Memory.HasBase)
                {
                    NDTOTEXT_APPEND("+");
                }

                switch (pOp->Info.Memory.IndexSize)
                {
                case ND_SIZE_8BIT:
                    if ((Instrux->EncMode != ND_ENCM_LEGACY) || Instrux->HasRex || Instrux->HasRex2)
                    {
                        NDTOTEXT_APPEND(gReg8Bit64[pOp->Info.Memory.Index]);
                    }
                    else
                    {
                        NDTOTEXT_APPEND(gReg8Bit[pOp->Info.Memory.Index]);
                    }
                    break;
                case ND_SIZE_16BIT:
                    NDTOTEXT_APPEND(gReg16Bit[pOp->Info.Memory.Index]);
                    break;
                case ND_SIZE_32BIT:
                    NDTOTEXT_APPEND(gReg32Bit[pOp->Info.Memory.Index]);
                    break;
                case ND_SIZE_64BIT:
                    NDTOTEXT_APPEND(gReg64Bit[pOp->Info.Memory.Index]);
                    break;
                case ND_SIZE_128BIT:
                    NDTOTEXT_APPEND(gRegXmm[pOp->Info.Memory.Index]);
                    break;
                case ND_SIZE_256BIT:
                    NDTOTEXT_APPEND(gRegYmm[pOp->Info.Memory.Index]);
                    break;
                case ND_SIZE_512BIT:
                    NDTOTEXT_APPEND(gRegZmm[pOp->Info.Memory.Index]);
                    break;
                default:
                    return ND_STATUS_INVALID_INSTRUX;
                }

                // If index is present, scale is also present.
                if (pOp->Info.Memory.Scale != 1 && !pOp->Info.Memory.IsMib)
                {
                    status = NdSprintf(temp, sizeof(temp), "*%d", pOp->Info.Memory.Scale);
                    if (!ND_SUCCESS(status))
                    {
                        return status;
                    }

                    NDTOTEXT_APPEND(temp);
                }
            }

            // Handle displacement.
            if (pOp->Info.Memory.HasDisp)
            {
                ND_UINT64 normDisp, disp;

                disp = pOp->Info.Memory.Disp;

                // If this is direct addressing (O operand) or we don't have base or index, than we don't normalize
                // the displacement, since it is used as a direct offset. Note that the second condition also
                // includes the RIP-relative case.
                if (pOp->Info.Memory.IsDirect || !(pOp->Info.Memory.HasBase || pOp->Info.Memory.HasIndex))
                {
                    normDisp = disp;
                }
                else
                {
                    switch (pOp->Info.Memory.DispSize)
                    {
                    case 1:
                        normDisp = ((disp & 0x80) ? ~disp + 1ULL : disp) & 0xFF;
                        break;
                    case 2:
                        normDisp = ((disp & 0x8000) ? ~disp + 1ULL : disp) & 0xFFFF;
                        break;
                    case 4:
                        normDisp = ((disp & 0x80000000) ? ~disp + 1ULL : disp) & 0xFFFFFFFF;
                        break;
                    default:
                        normDisp = disp;
                        break;
                    }

                    // Handle compressed displacement. It is fine to cast the normDisp to ND_UINT32, as the
                    // compressed displacement only works with ND_UINT8 displacements. Also, in this phase,
                    // the normDisp is converted to a positive quantity, so no sign-extension is needed.
                    if (pOp->Info.Memory.HasCompDisp)
                    {
                        normDisp = (ND_UINT64)(ND_UINT32)normDisp * pOp->Info.Memory.CompDispSize;
                    }
                }


                // Now displacement.
                if (pOp->Info.Memory.HasBase || pOp->Info.Memory.HasIndex)
                {
                    ND_BOOL sign;

                    if (ND_GET_SIGN(8, pOp->Info.Memory.Disp))
                    {
                        sign = ND_TRUE;
                    }
                    else
                    {
                        sign = ND_FALSE;
                    }

                    NDTOTEXT_APPEND(sign ? "-" : "+");
                }

                if (pOp->Info.Memory.IsRipRel)
                {
                    ND_UINT64 target = disp + Rip + Instrux->Length;

                    if (Instrux->AddrMode == ND_ADDR_32)
                    {
                        target &= 0xFFFFFFFF;
                    }

                    status = NdSprintf(temp, sizeof(temp), "rel 0x%llx", target);
                }
                else
                {
                    ND_UINT8 trimSize;

                    trimSize = (Instrux->AddrMode == ND_ADDR_16) ? 2 : ((Instrux->AddrMode == ND_ADDR_32) ? 4 : 8);

                    // Truncate the displacement size to the size of the address length.
                    normDisp = ND_TRIM(trimSize, normDisp);

                    status = NdSprintf(temp, sizeof(temp), "0x%llx", normDisp);
                }
                if (!ND_SUCCESS(status))
                {
                    return status;
                }

                NDTOTEXT_APPEND(temp);
            }

            // And the ending "]"
            NDTOTEXT_APPEND("]");

            // Handle memory broadcast.
            if (pOp->Info.Memory.HasBroadcast)
            {
                status = NdSprintf(temp, sizeof(temp), "{1to%d}", pOp->Info.Memory.Broadcast.Count);
                if (!ND_SUCCESS(status))
                {
                    return status;
                }

                NDTOTEXT_APPEND(temp);
            }
        }
        break;

        default:
            return ND_STATUS_INVALID_INSTRUX;
        }

        // Handle masking.
        if (pOp->Decorator.HasMask)
        {
            // No need for Msk range checks, as it is limited by the data type representation - 3 bits.
            //if (pOp->Decorator.Msk >= ND_MAX_MSK_REGS)
            //{
            //    return ND_STATUS_INVALID_INSTRUX;
            //}

            status = NdSprintf(temp, sizeof(temp), "{%s}", gRegMask[pOp->Decorator.Msk]);
            if (!ND_SUCCESS(status))
            {
                return status;
            }

            NDTOTEXT_APPEND(temp);
        }

        // Handle zeroing. Note that zeroing without masking is ignored.
        if (pOp->Decorator.HasZero && pOp->Decorator.HasMask)
        {
            NDTOTEXT_APPEND("{z}");
        }

        // If this is the last reg/mem operand, display {sae} and {er} decorators.
        if ((pOp->Type == ND_OP_MEM || pOp->Type == ND_OP_REG) && 
            (opIndex + 1 == Instrux->ExpOperandsCount || (opIndex + 2 == Instrux->ExpOperandsCount && Instrux->HasImm1)))
        {
            // Append Suppress All Exceptions decorator.
            if (Instrux->HasSae && !Instrux->HasEr)
            {
                // ER implies SAE, so if we have ER, we will list that.
                NDTOTEXT_APPEND("{sae}");
            }

            // Append Embedded Rounding decorator.
            if (Instrux->HasEr)
            {
                status = NdSprintf(temp, sizeof(temp), "{%s-sae}", gEmbeddedRounding[Instrux->RoundingMode]);
                if (!ND_SUCCESS(status))
                {
                    return status;
                }

                NDTOTEXT_APPEND(temp);
            }
        }
    }

#undef NDTOTEXT_APPEND

    return ND_STATUS_SUCCESS;
}

NDSTATUS
NdToText(
    const INSTRUX *Instrux,
    ND_UINT64 Rip,
    ND_UINT32 BufferSize,
    char *Buffer
)
{
    return NdToTextInternal((INSTRUX_MINI *)Instrux, Instrux->Operands, Rip, BufferSize, Buffer);
}


NDSTATUS
NdToTextMini(
    const INSTRUX_MINI *Instrux,
    ND_UINT64 Rip,
    ND_UINT32 BufferSize,
    char *Buffer
)
{
    return NdToTextInternal(Instrux, ND_NULL, Rip, BufferSize, Buffer);
}
#else
NDSTATUS
NdToText(
    const INSTRUX *Instrux,
    ND_UINT64 Rip,
    ND_UINT32 BufferSize,
    char *Buffer
)
{
    UNREFERENCED_PARAMETER(Instrux);
    UNREFERENCED_PARAMETER(Rip);

    // At least make sure the buffer is ND_NULL-terminated so integrators can use NdToText without checking if the
    // BDDISASM_NO_FORMAT macro is defined. This makes switching between versions with formatting and versions without
    // formatting easier.
    if (Buffer != ND_NULL && BufferSize >= 1)
    {
        *Buffer = '\0';
    }

    return ND_STATUS_SUCCESS;
}

NDSTATUS
NdToTextMini(
    const INSTRUX_MINI *Instrux,
    ND_UINT64 Rip,
    ND_UINT32 BufferSize,
    char *Buffer
)
{
    UNREFERENCED_PARAMETER(Instrux);
    UNREFERENCED_PARAMETER(Rip);

    // At least make sure the buffer is ND_NULL-terminated so integrators can use NdToText without checking if the
    // BDDISASM_NO_FORMAT macro is defined. This makes switching between versions with formatting and versions without
    // formatting easier.
    if (Buffer != ND_NULL && BufferSize >= 1)
    {
        *Buffer = '\0';
    }

    return ND_STATUS_SUCCESS;
}
#endif // !defined(BDDISASM_NO_MNEMONIC) && !defined(BDDISASM_NO_FORMAT)
