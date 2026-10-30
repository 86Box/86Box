/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- legacy VGA path.
 *
 *          The VGA ports and the A0000 window through the emulator's SVGA
 *          core, plus what the chip adds to them: the extended CRTC
 *          registers behind the CR1F lock, the two 32 KB page registers,
 *          the instance memory window of CR1C bit 7, the DDC pins and the
 *          real-mode access ports at 3D0-3D3 (CR38), through which a DOS
 *          program or the video BIOS reaches the chip's registers.
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>
#include <86box/nv/vid_nv4.h>

/* The legacy decode is on while PCI_NV_21 VGA is set, the ports with
   the PCI command register's I/O enable and the A0000 window with its
   memory enable. The command register comes out of reset with both on
   (as the RIVA 128's), since the emulator's system BIOSes bring the
   primary display up before they enumerate PCI. Windows turns both off
   on a display adapter it has no driver for; drawing on through the
   window with the ports gone garbles the screen. */
static int
nv4_vga_io_enabled(const nv4_t *nv4)
{
    return (nv4->pci_regs[0x54] & 0x01) && (nv4->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO);
}

static int
nv4_vga_mem_enabled(const nv4_t *nv4)
{
    return (nv4->pci_regs[0x54] & 0x01) && (nv4->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM);
}

/* ------------------------------------------------------------------ */
/* Banking.                                                            */
/* ------------------------------------------------------------------ */
/* CR1D is the write page and CR1E the read page, each 9 bits with the
   ninth in CR29 (bit 1 for CR1D, bit 0 for CR1E), in 32 KB units: the
   video BIOS sets a 64 KB VESA window by writing twice the bank number.
   CR1C bit 2 (sequential chain-4) makes chain-4 accesses address memory
   byte for byte, which is the svga core's packed chain-4. */
void
nv4_vga_update_banks(nv4_t *nv4)
{
    svga_t  *svga  = &nv4->svga;
    uint32_t wpage = svga->crtc[NV4_CR_PAGE0] | ((svga->crtc[NV4_CR_PAGE_OVFL] & 0x02) << 7);
    uint32_t rpage = svga->crtc[NV4_CR_PAGE1] | ((svga->crtc[NV4_CR_PAGE_OVFL] & 0x01) << 8);

    svga->write_bank    = wpage << 15;
    svga->read_bank     = rpage << 15;
    svga->packed_chain4 = !!(svga->crtc[NV4_CR_ENH] & 0x04) || !!(svga->crtc[NV4_CR_PIXEL] & 0x03);
}

/* ------------------------------------------------------------------ */
/* The A0000 window.                                                   */
/* ------------------------------------------------------------------ */
/* With CR1C bit 7 set, the window reaches instance memory: the video
   BIOS copies itself through A000:0 into the first 64 KB of instance
   memory this way, from where the resman reads its revision. The 3D
   batch is drawn before any access, as for the linear framebuffer. */

/* Before an access of <n> bytes at <addr> in the window: the 3D work
   that reaches the VRAM bytes it maps to is drawn and waited for. Only
   packed chain-4 (the SVGA and VESA banked modes) maps the window onto
   VRAM byte for byte, at the bank (svga_read_common/svga_write_common:
   the four bytes of the aligned dword, for the latches and the plane
   mask); planar and text modes and the instance memory window wait for
   all of it. */
void
nv4_vga_mem_access(nv4_t *nv4, uint32_t addr, int n, int write)
{
    svga_t  *svga = &nv4->svga;
    uint32_t lo, hi;

    if ((svga->crtc[NV4_CR_ENH] & 0x80) || !((svga->chain4 && svga->packed_chain4) || svga->fb_only)
        || (svga->adv_flags & (FLAG_ADDR_BY8 | FLAG_ADDR_BY16))) {
        nv4_3d_sync(nv4, NV4_SYNC_VGA);
        return;
    }
    lo = svga_decode_addr(svga, addr, write);
    hi = svga_decode_addr(svga, addr + n - 1, write);
    if ((lo == 0xffffffff) && (hi == 0xffffffff))
        return; /* outside the window: no memory is reached */
    lo = ((lo & svga->decode_mask) & nv4->vram_mask) & ~3u;
    hi = ((hi & svga->decode_mask) & nv4->vram_mask) | 3u;
    if ((lo > hi) || (hi > nv4->vram_mask))
        nv4_3d_sync(nv4, NV4_SYNC_VGA);
    else
        nv4_3d_access(nv4, lo, hi, NV4_SYNC_VGA);
}

static uint8_t
nv4_vga_mem_read(uint32_t addr, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;

    nv4_vga_mem_access(nv4, addr, 1, 0);
    if (nv4->svga.crtc[NV4_CR_ENH] & 0x80)
        return nv4_ramin_read8(nv4, addr & 0xffff);
    return svga_read(addr, &nv4->svga);
}

static uint16_t
nv4_vga_mem_readw(uint32_t addr, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;

    nv4_vga_mem_access(nv4, addr, 2, 0);
    if (nv4->svga.crtc[NV4_CR_ENH] & 0x80)
        return nv4_ramin_read16(nv4, addr & 0xffff);
    return svga_readw(addr, &nv4->svga);
}

static uint32_t
nv4_vga_mem_readl(uint32_t addr, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;

    nv4_vga_mem_access(nv4, addr, 4, 0);
    if (nv4->svga.crtc[NV4_CR_ENH] & 0x80)
        return nv4_ramin_read32(nv4, addr & 0xffff);
    return svga_readl(addr, &nv4->svga);
}

static void
nv4_vga_mem_write(uint32_t addr, uint8_t val, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;

    nv4_vga_mem_access(nv4, addr, 1, 1);
    if (nv4->svga.crtc[NV4_CR_ENH] & 0x80)
        nv4_ramin_write8(nv4, addr & 0xffff, val);
    else
        svga_write(addr, val, &nv4->svga);
}

static void
nv4_vga_mem_writew(uint32_t addr, uint16_t val, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;

    nv4_vga_mem_access(nv4, addr, 2, 1);
    if (nv4->svga.crtc[NV4_CR_ENH] & 0x80)
        nv4_ramin_write16(nv4, addr & 0xffff, val);
    else
        svga_writew(addr, val, &nv4->svga);
}

static void
nv4_vga_mem_writel(uint32_t addr, uint32_t val, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;

    nv4_vga_mem_access(nv4, addr, 4, 1);
    if (nv4->svga.crtc[NV4_CR_ENH] & 0x80)
        nv4_ramin_write32(nv4, addr & 0xffff, val);
    else
        svga_writel(addr, val, &nv4->svga);
}

/* The window follows GR06 as on any VGA, and goes away with the VGA
   or memory decode. */
void
nv4_vga_update_mapping(nv4_t *nv4)
{
    svga_t *svga = &nv4->svga;

    if (!nv4_vga_mem_enabled(nv4)) {
        mem_mapping_disable(&svga->mapping);
        return;
    }

    mem_mapping_set_handler(&svga->mapping,
                            nv4_vga_mem_read, nv4_vga_mem_readw, nv4_vga_mem_readl,
                            nv4_vga_mem_write, nv4_vga_mem_writew, nv4_vga_mem_writel);
    mem_mapping_set_p(&svga->mapping, nv4);

    switch (svga->gdcreg[6] & 0x0c) {
        case 0x00: /* 128 KB at A0000 */
            mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
            svga->banked_mask = 0xffff;
            break;
        case 0x04: /* 64 KB at A0000 */
            mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
            svga->banked_mask = 0xffff;
            break;
        case 0x08: /* 32 KB at B0000 */
            mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
            svga->banked_mask = 0x7fff;
            break;
        case 0x0c: /* 32 KB at B8000 */
            mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
            svga->banked_mask = 0x7fff;
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Real-mode access: I/O 3D0-3D3 under CR38.                           */
/* ------------------------------------------------------------------ */
/* CR38 bit 0 enables the decode and bits 2:1 pick what the four ports
   are: the id code, the pointer, the data port at the pointer, or the
   data port that collects a dword and writes it when byte 3 is
   written. The pointer reaches the registers below 16 MB and the
   framebuffer above, at pointer - 16 MB: 25 bits on the TNT, 26 on the
   TNT2, whose video BIOS probes 32 MB through it. */
static uint32_t
nv4_rma_ptr_mask(const nv4_t *nv4)
{
    return (nv4->chip == NV4_CHIP_TNT2) ? 0x03fffffc : 0x01fffffc;
}


static uint32_t
nv4_rma_space_read(nv4_t *nv4, uint32_t addr, int len)
{
    if (addr >= 0x01000000) {
        uint32_t off = (addr - 0x01000000) & nv4->vram_mask;
        uint32_t ret = 0;
        for (int i = 0; i < len; i++)
            ret |= (uint32_t) nv4->svga.vram[(off + i) & nv4->vram_mask] << (i * 8);
        return ret;
    }
    return nv4_mmio_read(nv4, addr, len);
}

static void
nv4_rma_space_write(nv4_t *nv4, uint32_t addr, uint32_t val, int len)
{
    if (addr >= 0x01000000) {
        uint32_t off = (addr - 0x01000000) & nv4->vram_mask;
        for (int i = 0; i < len; i++)
            nv4->svga.vram[(off + i) & nv4->vram_mask] = val >> (i * 8);
        nv4->svga.changedvram[off >> 12] = nv4->svga.monitor->mon_changeframecount;
        return;
    }
    nv4_mmio_write(nv4, addr, val, len);
}

static uint32_t
nv4_rma_read(nv4_t *nv4, uint16_t port, int len)
{
    int      off = port & 3;
    uint32_t val;

    switch ((nv4->rma_mode >> 1) & 3) {
        case 0:
            val = NV4_RMA_ID_CODE;
            break;
        case 1:
            val = nv4->rma_ptr;
            break;
        case 2:
            return nv4_rma_space_read(nv4, nv4->rma_ptr + off, len);
        default:
            val = nv4->rma_data32;
            break;
    }
    val >>= off * 8;
    return (len == 4) ? val : (val & ((1u << (len * 8)) - 1));
}

static void
nv4_rma_write(nv4_t *nv4, uint16_t port, uint32_t val, int len)
{
    int      off  = port & 3;
    uint32_t mask = (len == 4) ? 0xffffffff : (((1u << (len * 8)) - 1) << (off * 8));

    switch ((nv4->rma_mode >> 1) & 3) {
        case 0:
            break;
        case 1:
            nv4->rma_ptr = ((nv4->rma_ptr & ~mask) | ((val << (off * 8)) & mask)) & nv4_rma_ptr_mask(nv4);
            break;
        case 2:
            nv4_rma_space_write(nv4, nv4->rma_ptr + off, val, len);
            break;
        default:
            nv4->rma_data32 = (nv4->rma_data32 & ~mask) | ((val << (off * 8)) & mask);
            if ((off + len) == 4) /* the access touched byte 3 */
                nv4_rma_space_write(nv4, nv4->rma_ptr, nv4->rma_data32, 4);
            break;
    }
}

static uint8_t
nv4_rma_inb(uint16_t port, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;

    if (!(nv4->rma_mode & 1))
        return 0xff;
    return nv4_rma_read(nv4, port, 1);
}

static uint16_t
nv4_rma_inw(uint16_t port, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;

    if (!(nv4->rma_mode & 1))
        return 0xffff;
    return nv4_rma_read(nv4, port, 2);
}

static uint32_t
nv4_rma_inl(uint16_t port, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;

    if (!(nv4->rma_mode & 1))
        return 0xffffffff;
    return nv4_rma_read(nv4, port, 4);
}

static void
nv4_rma_outb(uint16_t port, uint8_t val, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;

    if (nv4->rma_mode & 1)
        nv4_rma_write(nv4, port, val, 1);
}

static void
nv4_rma_outw(uint16_t port, uint16_t val, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;

    if (nv4->rma_mode & 1)
        nv4_rma_write(nv4, port, val, 2);
}

static void
nv4_rma_outl(uint16_t port, uint32_t val, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;

    if (nv4->rma_mode & 1)
        nv4_rma_write(nv4, port, val, 4);
}

/* The PRMIO view of the same registers (0x7100-0x710C), for completeness. */
uint32_t
nv4_prmio_read(nv4_t *nv4, uint32_t addr)
{
    switch (addr) {
        case NV4_PRMIO_RMA_ID:
            return NV4_RMA_ID_CODE;
        case NV4_PRMIO_RMA_PTR:
            return nv4->rma_ptr;
        case NV4_PRMIO_RMA_DATA:
            return nv4_rma_space_read(nv4, nv4->rma_ptr, 4);
        case NV4_PRMIO_RMA_DATA32:
            return nv4->rma_data32;
        default:
            return 0;
    }
}

void
nv4_prmio_write(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    switch (addr) {
        case NV4_PRMIO_RMA_PTR:
            nv4->rma_ptr = val & nv4_rma_ptr_mask(nv4);
            break;
        case NV4_PRMIO_RMA_DATA:
        case NV4_PRMIO_RMA_DATA32:
            nv4_rma_space_write(nv4, nv4->rma_ptr, val, 4);
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* VGA ports.                                                          */
/* ------------------------------------------------------------------ */
/* Extended CRTC registers: 0x19 and up, reachable once CR1F has been
   written with 0x57 (read/write) or 0x75 (read only). */
static int
nv4_cr_is_extended(uint8_t index)
{
    return index >= NV4_CR_RPC0;
}

uint8_t
nv4_vga_in(uint16_t addr, void *priv)
{
    nv4_t  *nv4  = (nv4_t *) priv;
    svga_t *svga = &nv4->svga;
    uint8_t idx;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3d4:
            return svga->crtcreg;
        case 0x3d5:
            idx = svga->crtcreg;
            if (idx == NV4_CR_LOCK)
                return nv4->cr_lock;
            if (nv4_cr_is_extended(idx)) {
                if (!nv4->cr_lock)
                    return 0x00;
                switch (idx) {
                    case NV4_CR_RPC1:
                        return svga->crtc[idx] | 0x28; /* bits 5 and 3 read as one */
                    case NV4_CR_FAB_ID:
                        return 0x00;
                    case NV4_CR_CHIP_ID:
                        return nv4->board->revision;
                    case NV4_CR_RL0:
                        return svga->displine & 0xff;
                    case NV4_CR_RL1:
                        return (svga->displine >> 8) & 0x07;
                    case NV4_CR_RMA:
                        return nv4->rma_mode;
                    case NV4_CR_DDC_STATUS:
                        return (i2c_gpio_get_sda(nv4->i2c) << 3) | (i2c_gpio_get_scl(nv4->i2c) << 2);
                    case NV4_CR_PCI_TO:
                        return 0x00; /* write only */
                    default:
                        return svga->crtc[idx];
                }
            }
            return svga->crtc[idx];
        default:
            if ((addr < 0x3b0) || (addr > 0x3df))
                return 0xff;
            return svga_in(addr, svga);
    }
}

void
nv4_vga_out(uint16_t addr, uint8_t val, void *priv)
{
    nv4_t  *nv4  = (nv4_t *) priv;
    svga_t *svga = &nv4->svga;
    uint8_t idx;
    uint8_t old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    if (nv4->dbg_regtrace && nv4->dbg_regtrace--) {
        if (addr == 0x3d5)
            pclog("nv4 regs: cr%02x = %02x\n", svga->crtcreg, val);
        else if ((addr == 0x3c5) || (addr == 0x3cf))
            pclog("nv4 regs: port %03x [%02x] = %02x\n", addr, (addr == 0x3c5) ? svga->seqaddr : svga->gdcaddr, val);
        else if (addr != 0x3d4)
            pclog("nv4 regs: port %03x = %02x\n", addr, val);
    }

    switch (addr) {
        case 0x3c2:
            svga_out(addr, val, svga);
            /* the core moved the 3Bx/3Dx decode; put ours back */
            nv4_vga_close(nv4);
            nv4_vga_init(nv4);
            return;
        case 0x3cf:
            svga_out(addr, val, svga);
            if ((svga->gdcaddr & 15) == 6)
                nv4_vga_update_mapping(nv4);
            return;
        case 0x3d4:
            svga->crtcreg = val;
            return;
        case 0x3d5:
            idx = svga->crtcreg;
            if (idx == NV4_CR_LOCK) {
                switch (val) {
                    case NV4_CR_LOCK_RW:
                        nv4->cr_lock = 3;
                        break;
                    case NV4_CR_LOCK_RO:
                        nv4->cr_lock = 1;
                        break;
                    default:
                        nv4->cr_lock = 0;
                        break;
                }
                return;
            }
            if (nv4_cr_is_extended(idx)) {
                if (nv4->cr_lock != 3)
                    return;
                switch (idx) {
                    case NV4_CR_RL0:
                    case NV4_CR_RL1:
                    case NV4_CR_FAB_ID:
                    case NV4_CR_CHIP_ID:
                    case NV4_CR_DDC_STATUS:
                        return; /* read only */
                    case NV4_CR_RMA:
                        nv4->rma_mode = val & 0x07;
                        return;
                    case NV4_CR_DDC_WR:
                        svga->crtc[idx] = val;
                        /* bit 5 drives SCL, bit 4 SDA (1 releases the line) */
                        i2c_gpio_set(nv4->i2c, !!(val & 0x20), !!(val & 0x10));
                        return;
                    case NV4_CR_PCI_TO:
                        nv4->pci_to = val;
                        return;
                    default:
                        break;
                }
                old             = svga->crtc[idx];
                svga->crtc[idx] = val;
                nv4_pcrtc_crtc_start_write(nv4, idx, val);
                switch (idx) {
                    case NV4_CR_ENH:
                    case NV4_CR_PAGE0:
                    case NV4_CR_PAGE1:
                    case NV4_CR_PAGE_OVFL:
                        nv4_vga_update_banks(nv4);
                        break;
                    default:
                        break;
                }
                if (old != val) {
                    svga->fullchange = svga->monitor->mon_changeframecount;
                    svga_recalctimings(svga);
                }
                return;
            }
            /* Standard registers: CR0-CR7 are write protected by CR11
               bit 7, with line compare bit 8 (CR7 bit 4) still writable. */
            if ((idx < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if ((idx == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            old             = svga->crtc[idx];
            svga->crtc[idx] = val;
            nv4_pcrtc_crtc_start_write(nv4, idx, val);
            if (old != val) {
                if ((idx < 0xe) || (idx > 0x10)) {
                    svga->fullchange = svga->monitor->mon_changeframecount;
                    svga_recalctimings(svga);
                }
            }
            return;
        default:
            if ((addr < 0x3b0) || (addr > 0x3df))
                return;
            svga_out(addr, val, svga);
            return;
    }
}

/* ------------------------------------------------------------------ */
/* Decode and lifecycle.                                               */
/* ------------------------------------------------------------------ */
/* The 3Bx ports only in monochrome mode (miscout bit 0 clear); the
   real-mode access ports 3D0-3D3 always (their handler checks CR38). */
void
nv4_vga_init(nv4_t *nv4)
{
    if (!nv4_vga_io_enabled(nv4))
        return;

    if (!(nv4->svga.miscout & 1))
        io_sethandler(0x03b0, 0x0010, nv4_vga_in, NULL, NULL, nv4_vga_out, NULL, NULL, nv4);
    io_sethandler(0x03c0, 0x0010, nv4_vga_in, NULL, NULL, nv4_vga_out, NULL, NULL, nv4);
    io_sethandler(0x03d0, 0x0004, nv4_rma_inb, nv4_rma_inw, nv4_rma_inl, nv4_rma_outb, nv4_rma_outw, nv4_rma_outl, nv4);
    io_sethandler(0x03d4, 0x000c, nv4_vga_in, NULL, NULL, nv4_vga_out, NULL, NULL, nv4);
}

void
nv4_vga_close(nv4_t *nv4)
{
    /* the svga core installs 3B0-3DF itself on a 3C2 write; take all of it */
    io_removehandler(0x03a0, 0x0040, nv4_vga_in, NULL, NULL, nv4_vga_out, NULL, NULL, nv4);
    io_removehandler(0x03d0, 0x0004, nv4_rma_inb, nv4_rma_inw, nv4_rma_inl, nv4_rma_outb, nv4_rma_outw, nv4_rma_outl, nv4);
}

void
nv4_vga_reset(nv4_t *nv4)
{
    svga_t *svga = &nv4->svga;

    nv4->cr_lock    = 0;
    nv4->rma_mode   = 0;
    nv4->rma_ptr    = 0;
    nv4->rma_data32 = 0;
    nv4->pci_to     = 0;

    /* the extended registers' reset values (the register descriptions) */
    memset(&svga->crtc[NV4_CR_RPC0], 0, sizeof(svga->crtc) - NV4_CR_RPC0);
    svga->crtc[NV4_CR_RPC1]        = 0x3f;
    svga->crtc[NV4_CR_FFLWM]       = 0x04;
    svga->crtc[NV4_CR_SHADOW_LOCK] = 0xfe;
    svga->crtc[NV4_CR_ILACE]       = 0xff;
    svga->crtc[NV4_CR_DDC_WR]      = 0x30;
    i2c_gpio_set(nv4->i2c, 1, 1);

    nv4_vga_update_banks(nv4);
    nv4_vga_close(nv4);
    nv4_vga_init(nv4);
}
