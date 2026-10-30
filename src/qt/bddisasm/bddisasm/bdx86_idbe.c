/*
 * Copyright (c) 2025 Bitdefender
 * SPDX-License-Identifier: Apache-2.0
 */
#include "include/bddisasm_crt.h"
#include "../inc/bddisasm.h"

#include "bdx86_tabledefs.h"
#include "bdx86_instructions.h"
#include "bdx86_mnemonics.h"


//
// NdIdbeGetEntry
//
const ND_IDBE *
NdIdbeGetEntry(
    ND_UINT32 Index
)
{
    if (Index >= ND_INSTRUCTIONS_COUNT)
    {
        return ND_NULL;
    }

    return &gInstructions[Index];
}


#ifndef BDDISASM_NO_MNEMONIC
//
// NdIdbeGetMnemonic
//
const char *
NdIdbeGetMnemonic(
    ND_UINT32 Index
)
{
    if (Index >= ND_MNEMONICS_COUNT)
    {
        return ND_NULL;
    }

    return gMnemonics[Index];
}
#endif