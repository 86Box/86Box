/*
 * Copyright (c) 2025 Bitdefender
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BDX86_OPERAND_H
#define BDX86_OPERAND_H

#include "../inc/bddisasm.h"
#include "bdx86_tabledefs.h"


NDSTATUS
NdParseOperand(
    const INSTRUX_MINI *Instrux,
    const ND_IDBE *Idbe,
    ND_OPERAND *Operand,
    ND_UINT64 Specifier
);

#endif // BDX86_OPERAND_H
