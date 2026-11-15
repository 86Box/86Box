/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the Maynard 16-bit ISA SCSI adapter.
 *
 * Authors: WNT50, <wnt2195@gmail.com>
 *
 *          Copyright 2026 WNT50.
 */
#ifndef SCSI_MAYNARD_H
#define SCSI_MAYNARD_H

#include <stdint.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/scsi_wd33c93.h>

typedef struct maynard_t {
    wd33c93_t wd;

    uint16_t  base;
    uint8_t   irq;
    uint8_t   dma;
    int       irq_state;
    uint8_t   reset_hold;

    /* Half of a 16-bit DMA word, when the channel is word wide. */
    uint16_t  dma_word;
    uint8_t   dma_half;

    pc_timer_t timer;
} maynard_t;

extern const device_t maynard_wd33c93_device;

#endif /*SCSI_MAYNARD_H*/
