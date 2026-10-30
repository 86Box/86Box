/*
 * Copyright (c) 2025 Bitdefender
 * SPDX-License-Identifier: Apache-2.0
 */
#include "include/bddisasm_crt.h"

// Core headers.
#include "../inc/bddisasm.h"

// Internal headers.
#include "bdx86_tabledefs.h"
#include "bdx86_operand.h"


//
// NdIsInstruxRipRelative
//
ND_BOOL
NdIsInstruxRipRelative(
    const INSTRUX *Instrux
)
//
// Provided for backwards compatibility with existing code that uses disasm 1.0
//
{
    if (ND_NULL == Instrux)
    {
        return ND_FALSE;
    }
    else
    {
        return Instrux->IsRipRelative != 0;
    }
}


//
// NdGetFullAccessMap
//
NDSTATUS
NdGetFullAccessMap(
    const INSTRUX *Instrux,
    ND_ACCESS_MAP *AccessMap
)
{
    ND_UINT32 i;
    const ND_OPERAND *pOp;

    // pre-init
    i = 0;
    pOp = (const ND_OPERAND *)ND_NULL;

    // validate
    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (ND_NULL == AccessMap)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < Instrux->OperandsCount; i++)
    {
        pOp = &Instrux->Operands[i];

        if (ND_OP_MEM == pOp->Type)
        {
            if (pOp->Info.Memory.IsStack)
            {
                AccessMap->StackAccess |= pOp->Access.Access;
                AccessMap->GprAccess[NDR_RSP] |= ND_ACCESS_READ|ND_ACCESS_WRITE;
                AccessMap->SegAccess[NDR_SS] |= ND_ACCESS_READ;
            }
            else
            {
                AccessMap->MemAccess |= pOp->Access.Access;

                if (pOp->Info.Memory.HasSeg)
                {
                    // No need for Seg range checks, as it is limited by the data type representation - 3 bits.
                    //if (pOp->Info.Memory.Seg >= ND_MAX_SEG_REGS)
                    //{
                    //    return ND_STATUS_INVALID_INSTRUX;
                    //}

                    AccessMap->SegAccess[pOp->Info.Memory.Seg] |= ND_ACCESS_READ;
                }

                if (pOp->Info.Memory.HasBase)
                {
                    // No need for Base range checks, as it is limited by the data type representation - 5 bits.
                    //if (pOp->Info.Memory.Base >= ND_MAX_GPR_REGS)
                    //{
                    //    return ND_STATUS_INVALID_INSTRUX;
                    //}

                    AccessMap->GprAccess[pOp->Info.Memory.Base] |= ND_ACCESS_READ;
                }

                if (pOp->Info.Memory.HasIndex)
                {
                    if (pOp->Info.Memory.IsVsib)
                    {
                        if (pOp->Info.Memory.Index >= ND_MAX_SSE_REGS)
                        {
                            return ND_STATUS_INVALID_INSTRUX;
                        }

                        AccessMap->SseAccess[pOp->Info.Memory.Index] |= ND_ACCESS_READ;
                    }
                    else
                    {
                        if (pOp->Info.Memory.Index >= ND_MAX_GPR_REGS)
                        {
                            return ND_STATUS_INVALID_INSTRUX;
                        }

                        AccessMap->GprAccess[pOp->Info.Memory.Index] |= ND_ACCESS_READ;
                    }
                }
            }
        }
        else if (ND_OP_REG == pOp->Type)
        {
            switch (pOp->Info.Register.Type)
            {
            case ND_REG_GPR:
                {
                    ND_UINT32 k;

                    for (k = 0; k < pOp->Info.Register.Count; k++)
                    {
                        if (pOp->Info.Register.IsHigh8)
                        {
                            if (pOp->Info.Register.Reg + k >= ND_MAX_GPR_REGS || pOp->Info.Register.Reg + k < 4)
                            {
                                return ND_STATUS_INVALID_INSTRUX;
                            }

                            AccessMap->GprAccess[pOp->Info.Register.Reg - 4 + k] |= pOp->Access.Access;
                        }
                        else
                        {
                            if (pOp->Info.Register.Reg + k >= ND_MAX_GPR_REGS)
                            {
                                return ND_STATUS_INVALID_INSTRUX;
                            }

                            AccessMap->GprAccess[pOp->Info.Register.Reg + k] |= pOp->Access.Access;
                        }
                    }
                }
                break;
            case ND_REG_SEG:
                if (pOp->Info.Register.Reg >= ND_MAX_SEG_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                AccessMap->SegAccess[pOp->Info.Register.Reg] |= pOp->Access.Access;
                break;
            case ND_REG_FPU:
                if (pOp->Info.Register.Reg >= ND_MAX_FPU_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                AccessMap->FpuAccess[pOp->Info.Register.Reg] |= pOp->Access.Access;
                break;
            case ND_REG_MMX:
                if (pOp->Info.Register.Reg >= ND_MAX_MMX_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                AccessMap->MmxAccess[pOp->Info.Register.Reg] |= pOp->Access.Access;
                break;
            case ND_REG_SSE:
                {
                    ND_UINT32 k;

                    for (k = 0; k < pOp->Info.Register.Count; k++)
                    {
                        if (pOp->Info.Register.Reg + k >= ND_MAX_SSE_REGS)
                        {
                            return ND_STATUS_INVALID_INSTRUX;
                        }

                        AccessMap->SseAccess[pOp->Info.Register.Reg + k] |= pOp->Access.Access;
                    }
                }
                break;
            case ND_REG_CR:
                if (pOp->Info.Register.Reg >= ND_MAX_CR_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                AccessMap->CrAccess[pOp->Info.Register.Reg] |= pOp->Access.Access;
                break;
            case ND_REG_DR:
                if (pOp->Info.Register.Reg >= ND_MAX_DR_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                AccessMap->DrAccess[pOp->Info.Register.Reg] |= pOp->Access.Access;
                break;
            case ND_REG_TR:
                if (pOp->Info.Register.Reg >= ND_MAX_TR_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                AccessMap->TrAccess[pOp->Info.Register.Reg] |= pOp->Access.Access;
                break;
            case ND_REG_BND:
                if (pOp->Info.Register.Reg >= ND_MAX_BND_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                AccessMap->BndAccess[pOp->Info.Register.Reg] |= pOp->Access.Access;
                break;
            case ND_REG_MSK:
                if (pOp->Info.Register.Reg >= ND_MAX_MSK_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                AccessMap->MskAccess[pOp->Info.Register.Reg] |= pOp->Access.Access;
                break;
            case ND_REG_SYS:
                if (pOp->Info.Register.Reg >= ND_MAX_SYS_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                AccessMap->SysAccess[pOp->Info.Register.Reg] |= pOp->Access.Access;
                break;
            case ND_REG_X87:
                if (pOp->Info.Register.Reg >= ND_MAX_X87_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                AccessMap->X87Access[pOp->Info.Register.Reg] |= pOp->Access.Access;
                break;
            case ND_REG_FLG:
                AccessMap->FlagsAccess |= pOp->Access.Access;
                break;
            case ND_REG_RIP:
                AccessMap->RipAccess |= pOp->Access.Access;
                break;
            case ND_REG_MXCSR:
                AccessMap->MxcsrAccess |= pOp->Access.Access;
                break;
            case ND_REG_PKRU:
                AccessMap->PkruAccess |= pOp->Access.Access;
                break;
            case ND_REG_SSP:
                AccessMap->SspAccess |= pOp->Access.Access;
                break;
            case ND_REG_TILE:
                if (pOp->Info.Register.Reg >= ND_MAX_TILE_REGS)
                {
                    return ND_STATUS_INVALID_INSTRUX;
                }

                AccessMap->TmmAccess[pOp->Info.Register.Reg] |= pOp->Access.Access;
                break;
            default:
                break;
            }
        }
        else if (ND_OP_BANK == pOp->Type)
        {
            ND_UINT8 j;

            // Bank registers access. This needs special handling. Note that LOADALL/LOADALLD is not supported, as
            // it is too old and it's not valid since the good old 486.
            if (ND_INS_FNSAVE == Instrux->Instruction)
            {
                for (j = 0; j < ND_MAX_FPU_REGS; j++)
                {
                    AccessMap->FpuAccess[j] |= ND_ACCESS_READ;
                }
            }
            else if (ND_INS_FRSTOR == Instrux->Instruction)
            {
                for (j = 0; j < ND_MAX_FPU_REGS; j++)
                {
                    AccessMap->FpuAccess[j] |= ND_ACCESS_WRITE;
                }
            }

            if ((ND_INS_XSAVE == Instrux->Instruction)  || (ND_INS_XSAVEOPT == Instrux->Instruction) ||
                (ND_INS_XSAVES == Instrux->Instruction) || (ND_INS_XSAVEC == Instrux->Instruction))
            {
                for (j = 0; j < ND_MAX_SSE_REGS; j++)
                {
                    AccessMap->SseAccess[j] |= ND_ACCESS_READ;
                }
            }
            else if ((ND_INS_XRSTOR == Instrux->Instruction) || (ND_INS_XRSTORS == Instrux->Instruction))
            {
                for (j = 0; j < ND_MAX_SSE_REGS; j++)
                {
                    AccessMap->SseAccess[j] |= ND_ACCESS_WRITE;
                }
            }
        }
    }

    return ND_STATUS_SUCCESS;
}


//
// NdGetOperandRlut
//
NDSTATUS
NdGetOperandRlut(
    const INSTRUX *Instrux,
    ND_OPERAND_RLUT *Rlut
)
{
    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (ND_NULL == Rlut)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    // Initialize the RLUT. 
    nd_memset(Rlut, 0, sizeof(*Rlut));

    for (ND_UINT8 i = 0; i < Instrux->OperandsCount; i++)
    {
        if (!!(Instrux->Operands[i].Access.Access & ND_ACCESS_ANY_WRITE))
        {
            // We only care about the first 2 destination operands.
            if (Rlut->Dst1 == ND_NULL)
            {
                Rlut->Dst1 = (PND_OPERAND)&Instrux->Operands[i];
            }
            else if (Rlut->Dst2 == ND_NULL)
            {
                Rlut->Dst2 = (PND_OPERAND)&Instrux->Operands[i];
            }
        }

        if (!!(Instrux->Operands[i].Access.Access & ND_ACCESS_ANY_READ))
        {
            // We only care about the first 4 source operands.
            if (Rlut->Src1 == ND_NULL)
            {
                Rlut->Src1 = (PND_OPERAND)&Instrux->Operands[i];
            }
            else if (Rlut->Src2 == ND_NULL)
            {
                Rlut->Src2 = (PND_OPERAND)&Instrux->Operands[i];
            }
            else if (Rlut->Src3 == ND_NULL)
            {
                Rlut->Src3 = (PND_OPERAND)&Instrux->Operands[i];
            }
            else if (Rlut->Src4 == ND_NULL)
            {
                Rlut->Src4 = (PND_OPERAND)&Instrux->Operands[i];
            }
        }

        if (Instrux->Operands[i].Type == ND_OP_MEM)
        {
            // We only care about the first 2 memory operands.
            if (Rlut->Mem1 == ND_NULL)
            {
                Rlut->Mem1 = (PND_OPERAND)&Instrux->Operands[i];
            }
            else if (Rlut->Mem2 == ND_NULL)
            {
                Rlut->Mem2 = (PND_OPERAND)&Instrux->Operands[i];
            }

            if (Instrux->Operands[i].Info.Memory.IsStack)
            {
                Rlut->Stack = (PND_OPERAND)&Instrux->Operands[i];
            }
        }

        if (Instrux->Operands[i].Type == ND_OP_REG && Instrux->Operands->Flags.IsDefault)
        {
            switch (Instrux->Operands[i].Info.Register.Type)
            {
            case ND_REG_FLG:
                Rlut->Flags = (PND_OPERAND)&Instrux->Operands[i];
                break;
            case ND_REG_RIP:
                Rlut->Rip = (PND_OPERAND)&Instrux->Operands[i];;
                break;
            case ND_REG_SEG:
                if (Instrux->Operands[i].Info.Register.Reg == NDR_CS)
                {
                    Rlut->Cs = (PND_OPERAND)&Instrux->Operands[i];
                }
                else if (Instrux->Operands[i].Info.Register.Reg == NDR_SS)
                {
                    Rlut->Ss = (PND_OPERAND)&Instrux->Operands[i];
                }
                break;
            case ND_REG_GPR:
                if (Instrux->Operands[i].Info.Register.Reg < 8)
                {
                    *(&Rlut->Rax + Instrux->Operands[i].Info.Register.Reg) = (PND_OPERAND)&Instrux->Operands[i];
                }
                break;
            default:
                break;
            }
        }
    }

    return ND_STATUS_SUCCESS;
}


//
// NdGetInstructionClassMini
//
NDSTATUS
NdGetInstructionClassMini(
    const INSTRUX_MINI *Instrux,
    ND_INS_CLASS *Instruction
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }
    
    *Instruction = pIdbe->Instruction;

    return ND_STATUS_SUCCESS;
}


//
// NdGetInstructionCategoryMini
//
NDSTATUS 
NdGetInstructionCategoryMini(
    const INSTRUX_MINI *Instrux,
    ND_INS_CATEGORY *Category
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }
    
    *Category = pIdbe->Category;

    return ND_STATUS_SUCCESS;
}


//
// NdGetInstructionSetMini
//
NDSTATUS
NdGetInstructionSetMini(
    const INSTRUX_MINI *Instrux,
    ND_INS_SET *Set
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }
    
    *Set = pIdbe->IsaSet;

    return ND_STATUS_SUCCESS;
}


//
// NdGetInstructionSetMini
//
NDSTATUS 
NdGetAttributesMini(
    const INSTRUX_MINI *Instrux,
    ND_UINT64 *Attributes
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }
    
    *Attributes = pIdbe->Attributes;

    return ND_STATUS_SUCCESS;
}

//
// NdGetFlagsAccessMini
//
NDSTATUS 
NdGetFlagsAccessMini(
    const INSTRUX_MINI *Instrux,
    ND_RFLAGS *Tested,
    ND_RFLAGS *Modified,
    ND_RFLAGS *Set,
    ND_RFLAGS *Cleared,
    ND_RFLAGS *Undefined
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }

    Tested->Raw = pIdbe->TestedFlags;
    Modified->Raw = pIdbe->ModifiedFlags;
    Undefined->Raw = pIdbe->SetFlags & pIdbe->ClearedFlags;
    Set->Raw = pIdbe->SetFlags ^ Undefined->Raw;
    Cleared->Raw = pIdbe->ClearedFlags ^ Undefined->Raw;

    return ND_STATUS_SUCCESS;
}


//
// NdGetCpuidFlagMini
//
NDSTATUS
NdGetCpuidFlagMini(
    const INSTRUX_MINI *Instrux,
    ND_CPUID_FLAG *Cpuid
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }

    *((ND_UINT64*)Cpuid) = pIdbe->CpuidFlag;
    
    return ND_STATUS_SUCCESS;
}


//
// NdGetValidModesMini
//
NDSTATUS
NdGetValidModesMini(
    const INSTRUX_MINI *Instrux,
    ND_VALID_MODES *ValidModes
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }

    ValidModes->Raw = pIdbe->ValidModes;

    return ND_STATUS_SUCCESS;
}


//
// NdGetValidPrefixesMini
//
NDSTATUS
NdGetValidPrefixesMini(
    const INSTRUX_MINI *Instrux,
    ND_VALID_PREFIXES *ValidPrefixes
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }

    ValidPrefixes->Raw = pIdbe->ValidPrefixes;
    
    return ND_STATUS_SUCCESS;
}


//
// NdGetValidDecoratorsMini
//
NDSTATUS
NdGetValidDecoratorsMini(
    const INSTRUX_MINI *Instrux,
    ND_VALID_DECORATORS *ValidDecorators
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }

    ValidDecorators->Raw = pIdbe->ValidDecorators;
    
    return ND_STATUS_SUCCESS;
}


//
// NdGetExceptionTypeMini
//
NDSTATUS
NdGetExceptionTypeMini(
    const INSTRUX_MINI *Instrux,
    ND_EX_TYPE *ExceptionType
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }

    *ExceptionType = pIdbe->ExcType;
    
    return ND_STATUS_SUCCESS;
}


//
// NdGetTupleTypeMini
//
NDSTATUS 
NdGetTupleTypeMini(
    const INSTRUX_MINI *Instrux,
    ND_TUPLE *Tuple
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }

    *Tuple = pIdbe->TupleType;
    
    return ND_STATUS_SUCCESS;
}


//
// NdGetSimdExceptionsMini
//
NDSTATUS
NdGetSimdExceptionsMini(
    const INSTRUX_MINI *Instrux,
    ND_SIMD_EXCEPTIONS *SimdExceptions
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }

    SimdExceptions->Raw = pIdbe->SimdExc;
    
    return ND_STATUS_SUCCESS;
}


//
// NdGetFpuFlagsAccessMini
//
NDSTATUS
NdGetFpuFlagsAccessMini(
    const INSTRUX_MINI *Instrux,
    ND_FPU_FLAGS *FpuFlagsAccess
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }

    FpuFlagsAccess->Raw = pIdbe->FpuFlags;
    
    return ND_STATUS_SUCCESS;
}


//
// NdGetCsAccessMini
//
NDSTATUS
NdGetCsAccessMini(
    const INSTRUX_MINI *Instrux,
    ND_OPERAND_ACCESS *Access
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }

    Access->Access = pIdbe->CsAccess;
    
    return ND_STATUS_SUCCESS;
}


//
// NdGetRipAccessMini
//
NDSTATUS
NdGetRipAccessMini(
    const INSTRUX_MINI *Instrux,
    ND_OPERAND_ACCESS *Access
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }

    Access->Access = pIdbe->RipAccess;
    
    return ND_STATUS_SUCCESS;
}

//
// NdGetRflagsAccessMini
//
NDSTATUS
NdGetRflagsAccessMini(
    const INSTRUX_MINI *Instrux,
    ND_OPERAND_ACCESS *Access
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }

    Access->Access = pIdbe->RflAccess;
    
    return ND_STATUS_SUCCESS;
}


//
// NdGetMemoryAccessMini
//
NDSTATUS
NdGetMemoryAccessMini(
    const INSTRUX_MINI *Instrux,
    ND_OPERAND_ACCESS *Access
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }

    Access->Access = pIdbe->Mem2Access | (Instrux->ModRm.mod != 3 ? pIdbe->Mem1Access : 0);
    
    return ND_STATUS_SUCCESS;
}

//
// NdGetStackAccessMini
//
NDSTATUS
NdGetStackAccessMini(
    const INSTRUX_MINI *Instrux,
    ND_OPERAND_ACCESS *Access
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }

    Access->Access = pIdbe->StkAccess;
    
    return ND_STATUS_SUCCESS;
}

//
// NdGetStackAccessMini
//
NDSTATUS
NdGetStackWordsMini(
    const INSTRUX_MINI *Instrux,
    ND_UINT8 *StackWords
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }

    *StackWords = pIdbe->StkWords;
    
    return ND_STATUS_SUCCESS;
}


//
// NdGetOperandMini
//
NDSTATUS 
NdGetOperandMini(
    const INSTRUX_MINI *Instrux,
    ND_UINT8 Index,
    ND_OPERAND *Operand
)
{
    const ND_IDBE *pIdbe;
    
    if (ND_NULL == Instrux)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (ND_NULL == Operand)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    if (Index >= ND_MAX_OPERAND)
    {
        return ND_STATUS_INVALID_PARAMETER;
    }

    // Make sure Instrux is not malformed.
    if (Instrux->DefCode   > ND_CODE_64  ||
        Instrux->DefStack  > ND_STACK_64 ||
        Instrux->OpMode    > ND_OPSZ_64  ||
        Instrux->EfOpMode  > ND_OPSZ_64  ||
        Instrux->AddrMode  > ND_ADDR_64  ||
        Instrux->VecMode   > ND_VECM_512 ||
        Instrux->EfVecMode > ND_VECM_512)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_STATUS_INVALID_INSTRUX;
    }

    nd_memzero(Operand, sizeof(ND_OPERAND));

    return NdParseOperand(Instrux, pIdbe, Operand, pIdbe->Operands[Index]);
}


#ifndef BDDISASM_NO_MNEMONIC
//
// NdGetMnemonicMini
//
const char *
NdGetMnemonicMini(
    const INSTRUX_MINI *Instrux
)
{
    const ND_IDBE *pIdbe;

    if (ND_NULL == Instrux)
    {
        return ND_NULL;
    }

    pIdbe = NdIdbeGetEntry(Instrux->Idbe);
    if (ND_NULL == pIdbe)
    {
        return ND_NULL;
    }

    return NdIdbeGetMnemonic(pIdbe->Mnemonic);
}
#endif