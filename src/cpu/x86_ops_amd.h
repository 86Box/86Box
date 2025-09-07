/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          AMD SYSCALL and SYSRET CPU Instructions.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Copyright 2016-2018 Miran Grca.
 */
static int
opSYSCALL(uint32_t fetchdat)
{
    int ret;

    ILLEGAL_ON(!(msr.amd_efer & 0x0000000000000001));

    ret = syscall_op(fetchdat);

    if (ret <= 1)
        CPU_BLOCK_END();

    return ret;
}

static int
opSYSRET(uint32_t fetchdat)
{
    int ret;

    ILLEGAL_ON(!(msr.amd_efer & 0x0000000000000001));

    ret = sysret(fetchdat);

    if (ret <= 1)
        CPU_BLOCK_END();

    return ret;
}
