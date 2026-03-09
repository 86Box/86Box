/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Generic QIC-02 interface.
 *
 *
 * Authors: 
 *
 *          Copyright 2025 seal331.
 */

/* This code is 100% AI-free */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/wangtek_qic.h>

void
wangtek_qic_sel_drive_0()
{
    /* For now we'll only support one drive, but still, error out while we're implementing */
    fatal("Unimplemented QIC command SELECT DRIVE 0");
}

void
wangtek_qic_sel_drive_1()
{
    /* For now we'll only support one drive (so this'll be a noop later),
     * but still, error out while we're implementing
     */
    fatal("Unimplemented QIC command SELECT DRIVE 1");
}

void
wangtek_qic_sel_drive_2()
{
    /* For now we'll only support one drive (so this'll be a noop later),
     * but still, error out while we're implementing
     */
    fatal("Unimplemented QIC command SELECT DRIVE 2");
}

void
wangtek_qic_rew_to_bot()
{
    fatal("Unimplemented QIC command REWIND TO BOT");
}

void
wangtek_qic_erase_tape()
{
    fatal("Unimplemented QIC command ERASE TAPE");
}

void
wangtek_qic_init_tape()
{
    fatal("Unimplemented QIC command INITIALIZE (RETENSION) TAPE");
}

void
wangtek_qic_write_data()
{
    fatal("Unimplemented QIC command WRITE DATA");
}

void
wangtek_qic_write_file_mark()
{
    fatal("Unimplemented QIC command WRITE FILE MARK");
}

void
wangtek_qic_read_data()
{
    // TEMPORARY currently just raises EXCEPTION
}

void
wangtek_qic_read_file_mark()
{
    fatal("Unimplemented QIC command READ FILE MARK");
}

void
wangtek_qic_read_status()
{
    /* Yes, this is in the official QIC-02 standard, this isn't implementation-defined */
    fatal("Unimplemented QIC command READ STATUS");
}
