/*
 * Copyright (c) 2025 Bitdefender
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BDX86_API_MINI_H
#define BDX86_API_MINI_H

#include "bdx86_core.h"


NDSTATUS
NdGetInstructionClassMini(
    const INSTRUX_MINI *Instrux,
    ND_INS_CLASS *InstructionClass
);

NDSTATUS
NdGetInstructionCategoryMini(
    const INSTRUX_MINI *Instrux,
    ND_INS_CATEGORY *Category
);

NDSTATUS
NdGetInstructionSetMini(
    const INSTRUX_MINI *Instrux,
    ND_INS_SET *Set
);

NDSTATUS
NdGetAttributesMini(
    const INSTRUX_MINI *Instrux,
    ND_UINT64 *Attributes
);

NDSTATUS 
NdGetFlagsAccessMini(
    const INSTRUX_MINI *Instrux,
    ND_RFLAGS *Tested,
    ND_RFLAGS *Modified,
    ND_RFLAGS *Set,
    ND_RFLAGS *Cleared,
    ND_RFLAGS *Undefined
);

NDSTATUS 
NdGetCpuidFlagMini(
    const INSTRUX_MINI *Instrux,
    ND_CPUID_FLAG *Cpuid
);

NDSTATUS
NdGetValidModesMini(
    const INSTRUX_MINI *Instrux,
    ND_VALID_MODES *ValidModes
);

NDSTATUS
NdGetValidPrefixesMini(
    const INSTRUX_MINI *Instrux,
    ND_VALID_PREFIXES *ValidPrefixes
);

NDSTATUS
NdGetValidDecoratorsMini(
    const INSTRUX_MINI *Instrux,
    ND_VALID_DECORATORS *ValidDecorators
);

NDSTATUS 
NdGetExceptionTypeMini(
    const INSTRUX_MINI *Instrux,
    ND_EX_TYPE *ExceptionType
);

NDSTATUS 
NdGetTupleTypeMini(
    const INSTRUX_MINI *Instrux,
    ND_TUPLE *Tuple
);

NDSTATUS
NdGetSimdExceptionsMini(
    const INSTRUX_MINI *Instrux,
    ND_SIMD_EXCEPTIONS *SimdExceptions
);

NDSTATUS
NdGetFpuFlagsAccessMini(
    const INSTRUX_MINI *Instrux,
    ND_FPU_FLAGS *FpuFlagsAccess
);

NDSTATUS
NdGetCsAccessMini(
    const INSTRUX_MINI *Instrux,
    ND_OPERAND_ACCESS *Access
);

NDSTATUS
NdGetRipAccessMini(
    const INSTRUX_MINI *Instrux,
    ND_OPERAND_ACCESS *Access
);

NDSTATUS
NdGetRflagsAccessMini(
    const INSTRUX_MINI *Instrux,
    ND_OPERAND_ACCESS *Access
);

NDSTATUS
NdGetMemoryAccessMini(
    const INSTRUX_MINI *Instrux,
    ND_OPERAND_ACCESS *Access
);

NDSTATUS
NdGetStackAccessMini(
    const INSTRUX_MINI *Instrux,
    ND_OPERAND_ACCESS *Access
);

NDSTATUS
NdGetStackWordsMini(
    const INSTRUX_MINI *Instrux,
    ND_UINT8 *StackWords
);

NDSTATUS
NdGetOperandMini(
    const INSTRUX_MINI *Instrux,
    ND_UINT8 Index,
    ND_OPERAND *Operand
);

#ifndef BDDISASM_NO_MNEMONIC
const char *
NdGetMnemonicMini(
    const INSTRUX_MINI *Instrux
);
#endif


#endif // BDX86_API_MINI_H
