/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implement I/O ports and their operations.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include "cpu.h"
#include <86box/m_amstrad.h>
#include <86box/pci.h>
#include <86box/mem.h>

#define NPORTS 65536 /* PC/AT supports 64K ports */

typedef struct _io_ {
    uint8_t (*inb)(uint16_t addr, void *priv);
    uint16_t (*inw)(uint16_t addr, void *priv);
    uint32_t (*inl)(uint16_t addr, void *priv);

    void (*outb)(uint16_t addr, uint8_t val, void *priv);
    void (*outw)(uint16_t addr, uint16_t val, void *priv);
    void (*outl)(uint16_t addr, uint32_t val, void *priv);

    void *priv;

    struct _io_ *prev, *next;
} io_t;

typedef struct {
    uint8_t   enable;
    uint16_t  base;
    uint16_t  size;
    void    (*func)(int size, uint16_t addr, uint8_t write, uint8_t val, void *priv);
    void     *priv;
} io_trap_t;

int   initialized = 0;
io_t *io[NPORTS];
io_t *io_last[NPORTS];

// #define ENABLE_IO_LOG 1
#ifdef ENABLE_IO_LOG
int io_do_log = ENABLE_IO_LOG;

static void
io_log(const char *fmt, ...)
{
    va_list ap;

    if (io_do_log) {
        va_start(ap, fmt);
        if (CS == 0xf000)
            pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define io_log(fmt, ...)
#endif

uint16_t last_port_read = 0xffff;

static void
print_lpr(void)
{
    FILE *f = fopen("d:\\86boxnew\\ndiags.dmp", "wb");
    fwrite(&(ram[0x24c30]), 1, 65536, f);
    fclose(f);

    pclog("last_port_read = %04X\n", last_port_read);
}

void
io_init(void)
{
    int   c;
    io_t *p;
    io_t *q;

    if (!initialized) {
        for (c = 0; c < NPORTS; c++)
            io[c] = io_last[c] = NULL;
        initialized = 1;
    }

    for (c = 0; c < NPORTS; c++) {
        if (io_last[c]) {
            /* Port c has at least one handler. */
            p = io_last[c];
            /* After this loop, p will have the pointer to the first handler. */
            while (p) {
                q = p->prev;
                free(p);
                p = q;
            }
            p = NULL;
        }

        /* io[c] should be NULL. */
        io[c] = io_last[c] = NULL;
    }

    atexit(print_lpr);
}

void
io_sethandler_common(uint16_t base, int size,
                     uint8_t (*inb)(uint16_t addr, void *priv),
                     uint16_t (*inw)(uint16_t addr, void *priv),
                     uint32_t (*inl)(uint16_t addr, void *priv),
                     void (*outb)(uint16_t addr, uint8_t val, void *priv),
                     void (*outw)(uint16_t addr, uint16_t val, void *priv),
                     void (*outl)(uint16_t addr, uint32_t val, void *priv),
                     void *priv, int step)
{
    io_t *p;
    io_t *q = NULL;

    for (int c = 0; c < size; c += step) {
        p = io_last[base + c];
        q = (io_t *) malloc(sizeof(io_t));
        memset(q, 0, sizeof(io_t));
        if (p) {
            p->next = q;
            q->prev = p;
        } else {
            io[base + c] = q;
            q->prev      = NULL;
        }

        q->inb = inb;
        q->inw = inw;
        q->inl = inl;

        q->outb = outb;
        q->outw = outw;
        q->outl = outl;

        q->priv = priv;
        q->next = NULL;

        io_last[base + c] = q;

        q = NULL;
    }
}

void
io_removehandler_common(uint16_t base, int size,
                        uint8_t (*inb)(uint16_t addr, void *priv),
                        uint16_t (*inw)(uint16_t addr, void *priv),
                        uint32_t (*inl)(uint16_t addr, void *priv),
                        void (*outb)(uint16_t addr, uint8_t val, void *priv),
                        void (*outw)(uint16_t addr, uint16_t val, void *priv),
                        void (*outl)(uint16_t addr, uint32_t val, void *priv),
                        void *priv, int step)
{
    io_t *p;
    io_t *q;

    for (int c = 0; c < size; c += step) {
        p = io[base + c];
        if (!p)
            continue;
        while (p) {
            q = p->next;
            if ((p->inb == inb) && (p->inw == inw) && (p->inl == inl) && (p->outb == outb) && (p->outw == outw) && (p->outl == outl) && (p->priv == priv)) {
                if (p->prev)
                    p->prev->next = p->next;
                else
                    io[base + c] = p->next;
                if (p->next)
                    p->next->prev = p->prev;
                else
                    io_last[base + c] = p->prev;
                free(p);
                p = NULL;
                break;
            }
            p = q;
        }
    }
}

void
io_handler_common(int set, uint16_t base, int size,
                  uint8_t (*inb)(uint16_t addr, void *priv),
                  uint16_t (*inw)(uint16_t addr, void *priv),
                  uint32_t (*inl)(uint16_t addr, void *priv),
                  void (*outb)(uint16_t addr, uint8_t val, void *priv),
                  void (*outw)(uint16_t addr, uint16_t val, void *priv),
                  void (*outl)(uint16_t addr, uint32_t val, void *priv),
                  void *priv, int step)
{
    if (set)
        io_sethandler_common(base, size, inb, inw, inl, outb, outw, outl, priv, step);
    else
        io_removehandler_common(base, size, inb, inw, inl, outb, outw, outl, priv, step);
}

void
io_sethandler(uint16_t base, int size,
              uint8_t (*inb)(uint16_t addr, void *priv),
              uint16_t (*inw)(uint16_t addr, void *priv),
              uint32_t (*inl)(uint16_t addr, void *priv),
              void (*outb)(uint16_t addr, uint8_t val, void *priv),
              void (*outw)(uint16_t addr, uint16_t val, void *priv),
              void (*outl)(uint16_t addr, uint32_t val, void *priv),
              void *priv)
{
    io_sethandler_common(base, size, inb, inw, inl, outb, outw, outl, priv, 1);
}

void
io_removehandler(uint16_t base, int size,
                 uint8_t (*inb)(uint16_t addr, void *priv),
                 uint16_t (*inw)(uint16_t addr, void *priv),
                 uint32_t (*inl)(uint16_t addr, void *priv),
                 void (*outb)(uint16_t addr, uint8_t val, void *priv),
                 void (*outw)(uint16_t addr, uint16_t val, void *priv),
                 void (*outl)(uint16_t addr, uint32_t val, void *priv),
                 void *priv)
{
    io_removehandler_common(base, size, inb, inw, inl, outb, outw, outl, priv, 1);
}

void
io_handler(int set, uint16_t base, int size,
           uint8_t (*inb)(uint16_t addr, void *priv),
           uint16_t (*inw)(uint16_t addr, void *priv),
           uint32_t (*inl)(uint16_t addr, void *priv),
           void (*outb)(uint16_t addr, uint8_t val, void *priv),
           void (*outw)(uint16_t addr, uint16_t val, void *priv),
           void (*outl)(uint16_t addr, uint32_t val, void *priv),
           void *priv)
{
    io_handler_common(set, base, size, inb, inw, inl, outb, outw, outl, priv, 1);
}

void
io_sethandler_interleaved(uint16_t base, int size,
                          uint8_t (*inb)(uint16_t addr, void *priv),
                          uint16_t (*inw)(uint16_t addr, void *priv),
                          uint32_t (*inl)(uint16_t addr, void *priv),
                          void (*outb)(uint16_t addr, uint8_t val, void *priv),
                          void (*outw)(uint16_t addr, uint16_t val, void *priv),
                          void (*outl)(uint16_t addr, uint32_t val, void *priv),
                          void *priv)
{
    io_sethandler_common(base, size, inb, inw, inl, outb, outw, outl, priv, 2);
}

void
io_removehandler_interleaved(uint16_t base, int size,
                             uint8_t (*inb)(uint16_t addr, void *priv),
                             uint16_t (*inw)(uint16_t addr, void *priv),
                             uint32_t (*inl)(uint16_t addr, void *priv),
                             void (*outb)(uint16_t addr, uint8_t val, void *priv),
                             void (*outw)(uint16_t addr, uint16_t val, void *priv),
                             void (*outl)(uint16_t addr, uint32_t val, void *priv),
                             void *priv)
{
    io_removehandler_common(base, size, inb, inw, inl, outb, outw, outl, priv, 2);
}

void
io_handler_interleaved(int set, uint16_t base, int size,
                       uint8_t (*inb)(uint16_t addr, void *priv),
                       uint16_t (*inw)(uint16_t addr, void *priv),
                       uint32_t (*inl)(uint16_t addr, void *priv),
                       void (*outb)(uint16_t addr, uint8_t val, void *priv),
                       void (*outw)(uint16_t addr, uint16_t val, void *priv),
                       void (*outl)(uint16_t addr, uint32_t val, void *priv),
                       void *priv)
{
    io_handler_common(set, base, size, inb, inw, inl, outb, outw, outl, priv, 2);
}

#ifdef USE_DEBUG_REGS_486
extern int trap;
/* Set trap for I/O address breakpoints. */
void
io_debug_check_addr(uint16_t addr)
{
    int i = 0;
    int set_trap = 0;

    if (!(dr[7] & 0xFF))
        return;
    
    if (!(cr4 & 0x8))
        return; /* No I/O debug trap. */

    for (i = 0; i < 4; i++) {
        uint16_t dr_addr = dr[i] & 0xFFFF;
        int breakpoint_enabled = !!(dr[7] & (0x3 << (2 * i)));
        int len_type_pair = ((dr[7] >> 16) & (0xF << (4 * i))) >> (4 * i);
        if (!breakpoint_enabled)
            continue;
        if ((len_type_pair & 3) != 2)
            continue;
        
        switch ((len_type_pair >> 2) & 3)
        {
            case 0x00:
                if (dr_addr == addr) {
                    set_trap = 1;
                    dr[6] |= (1 << i);
                }
                break;
            case 0x01:
                if ((dr_addr & ~1) == addr || ((dr_addr & ~1) + 1) == (addr + 1)) {
                    set_trap = 1;
                    dr[6] |= (1 << i);
                }
                break;
            case 0x03:
                dr_addr &= ~3;
                if (addr >= dr_addr && addr < (dr_addr + 4)) {
                    set_trap = 1;
                    dr[6] |= (1 << i);
                }
                break;
        }
    }
    if (set_trap)
        trap |= 4;
}
#endif

#include <86box/random.h>

uint8_t post_code = 0xc6;
uint8_t action = 0x00;

uint8_t
inb(uint16_t port)
{
    uint8_t ret = 0xff;
    io_t   *p;
    io_t   *q;
    int     found  = 0;
#ifdef ENABLE_IO_LOG
    int     qfound = 0;
#endif

#ifdef USE_DEBUG_REGS_486
    io_debug_check_addr(port);
#endif

    if ((pci_flags & FLAG_CONFIG_IO_ON) && (port >= pci_base) && (port < (pci_base + pci_size))) {
        ret = pci_read(port, NULL);
        found = 1;
#ifdef ENABLE_IO_LOG
        qfound = 1;
#endif
    } else if ((pci_flags & FLAG_CONFIG_DEV0_IO_ON) && (port >= 0xc000) && (port < 0xc100)) {
        ret = pci_read(port, NULL);
        found = 1;
#ifdef ENABLE_IO_LOG
        qfound = 1;
#endif
    } else {
        p = io[port];
        while (p) {
            q = p->next;
            if (p->inb) {
                ret &= p->inb(port, p->priv);
                found |= 1;
#ifdef ENABLE_IO_LOG
                qfound++;
#endif
            }
            p = q;
        }
    }

    if (amstrad_latch & 0x80000000) {
        if (port & 0x80)
            amstrad_latch = AMSTRAD_NOLATCH | 0x80000000;
        else if (port & 0x4000)
            amstrad_latch = AMSTRAD_SW10 | 0x80000000;
        else
            amstrad_latch = AMSTRAD_SW9 | 0x80000000;
    }

    if (!found)
        cycles -= io_delay;

    /* TriGem 486-BIOS MHz output. */
#if 0
    if (port == 0x1ed)
        ret = 0xfe;
#endif

#if 0
    if (port == 0x5f7)
        ret = 0xaf;
#endif

    if (port == 0x379)
        ret &= 0xf8;

    // io_log("[%04X:%08X] (%i, %i, %04i) in b(%04X) = %02X\n", CS, cpu_state.pc, in_smm, found, qfound, port, ret);

    // if (CS == 0xc000)
        // pclog("[%04X:%08X] [R] %04X = %02X\n", CS, cpu_state.pc, port, ret);

    // if (port == 0x62)
        // ret = 0xf2;

    // if (port == 0x62)
        // ret |= random_generate() & 0x80;

    // if ((port >= 0x60) && (port <= 0x66))
        // pclog("[%04X:%04X] [R] %04X = %02X\n", CS, cpu_state.pc, port, ret);

    if ((port != 0x0061) && (port != 0x0177) && (port != 0x01f7) && (port != 0x0376) && (port != 0x03c7) && (port != 0x03c8) && (port != 0x03c9) && (port != 0x03f6) && (port != 0x03da)) {
        io_log("[%04X:%08X] (%i, %i, %04i) in b(%04X) = %02X\n", CS, cpu_state.pc, in_smm, found, qfound, port, ret);
    }

    last_port_read = port;

    return ret;
}

void
outb(uint16_t port, uint8_t val)
{
    io_t *p;
    io_t *q;
    int   found  = 0;
#ifdef ENABLE_IO_LOG
    int   qfound = 0;
#endif

#ifdef USE_DEBUG_REGS_486
    io_debug_check_addr(port);
#endif

    if ((pci_flags & FLAG_CONFIG_IO_ON) && (port >= pci_base) && (port < (pci_base + pci_size))) {
        pci_write(port, val, NULL);
        found = 1;
#ifdef ENABLE_IO_LOG
        qfound = 1;
#endif
    } else if ((pci_flags & FLAG_CONFIG_DEV0_IO_ON) && (port >= 0xc000) && (port < 0xc100)) {
        pci_write(port, val, NULL);
        found = 1;
#ifdef ENABLE_IO_LOG
        qfound = 1;
#endif
    } else {
        p = io[port];
        while (p) {
            q = p->next;
            if (p->outb) {
                p->outb(port, val, p->priv);
                found |= 1;
#ifdef ENABLE_IO_LOG
                qfound++;
#endif
            }
            p = q;
        }
    }

    if (!found) {
        cycles -= io_delay;
#ifdef USE_DYNAREC
        if (cpu_use_dynarec && ((port == 0xeb) || (port == 0xed)))
            update_tsc();
#endif
    }

    // io_log("[%04X:%08X] (%i, %i, %04i) outb(%04X, %02X)\n", CS, cpu_state.pc, in_smm, found, qfound, port, val);

    if (port == 0x0068)
        action = val;

    if (port == 0x0080)
        post_code = val;

    // if (port == 0x3db)
        // pclog("[%04X:%04X] [W] %04X = %02X\n", CS, cpu_state.pc, port, val);

    if ((port != 0x0061) && (port != 0x00ed) && (port != 0x03c7) && (port != 0x03c8) && (port != 0x03c9)) {
        io_log("[%04X:%08X] (%i, %i, %04i) outb(%04X, %02X)\n", CS, cpu_state.pc, in_smm, found, qfound, port, val);
    }

    return;
}

uint16_t
inw(uint16_t port)
{
    io_t    *p;
    io_t    *q;
    uint16_t ret    = 0xffff;
    int      found  = 0;
#ifdef ENABLE_IO_LOG
    int      qfound = 0;
#endif
    uint8_t  ret8[2];

#ifdef USE_DEBUG_REGS_486
    io_debug_check_addr(port);
#endif

    if ((pci_flags & FLAG_CONFIG_IO_ON) && (port >= pci_base) && (port < (pci_base + pci_size))) {
        ret = pci_readw(port, NULL);
        found = 2;
#ifdef ENABLE_IO_LOG
        qfound = 1;
#endif
    } else if ((pci_flags & FLAG_CONFIG_DEV0_IO_ON) && (port >= 0xc000) && (port < 0xc100)) {
        ret = pci_readw(port, NULL);
        found = 2;
#ifdef ENABLE_IO_LOG
        qfound = 1;
#endif
    } else {
        p = io[port];
        while (p) {
            q = p->next;
            if (p->inw) {
                ret &= p->inw(port, p->priv);
                found |= 2;
#ifdef ENABLE_IO_LOG
                qfound++;
#endif
            }
            p = q;
        }

        ret8[0] = ret & 0xff;
        ret8[1] = (ret >> 8) & 0xff;
        for (uint8_t i = 0; i < 2; i++) {
            p = io[(port + i) & 0xffff];
            while (p) {
                q = p->next;
                if (p->inb && !p->inw) {
                    ret8[i] &= p->inb(port + i, p->priv);
                    found |= 1;
#ifdef ENABLE_IO_LOG
                    qfound++;
#endif
                }
                p = q;
            }
        }
        ret = (ret8[1] << 8) | ret8[0];
    }

    if (amstrad_latch & 0x80000000) {
        if (port & 0x80)
            amstrad_latch = AMSTRAD_NOLATCH | 0x80000000;
        else if (port & 0x4000)
            amstrad_latch = AMSTRAD_SW10 | 0x80000000;
        else
            amstrad_latch = AMSTRAD_SW9 | 0x80000000;
    }

    if (!found)
        cycles -= io_delay;

    if (1) {
        io_log("[%04X:%08X] (%i, %i, %04i) in w(%04X) = %04X\n", CS, cpu_state.pc, in_smm, found, qfound, port, ret);
    }

    return ret;
}

void
outw(uint16_t port, uint16_t val)
{
    io_t *p;
    io_t *q;
    int   found  = 0;
#ifdef ENABLE_IO_LOG
    int   qfound = 0;
#endif

#ifdef USE_DEBUG_REGS_486
    io_debug_check_addr(port);
#endif

    if ((pci_flags & FLAG_CONFIG_IO_ON) && (port >= pci_base) && (port < (pci_base + pci_size))) {
        pci_writew(port, val, NULL);
        found = 2;
#ifdef ENABLE_IO_LOG
        qfound = 1;
#endif
    } else if ((pci_flags & FLAG_CONFIG_DEV0_IO_ON) && (port >= 0xc000) && (port < 0xc100)) {
        pci_writew(port, val, NULL);
        found = 2;
#ifdef ENABLE_IO_LOG
        qfound = 1;
#endif
    } else {
        p = io[port];
        while (p) {
            q = p->next;
            if (p->outw) {
                p->outw(port, val, p->priv);
                found |= 2;
#ifdef ENABLE_IO_LOG
                qfound++;
#endif
            }
            p = q;
        }

        for (uint8_t i = 0; i < 2; i++) {
            p = io[(port + i) & 0xffff];
            while (p) {
                q = p->next;
                if (p->outb && !p->outw) {
                    p->outb(port + i, val >> (i << 3), p->priv);
                    found |= 1;
#ifdef ENABLE_IO_LOG
                    qfound++;
#endif
                }
                p = q;
            }
        }
    }

    if (!found) {
        cycles -= io_delay;
#ifdef USE_DYNAREC
        if (cpu_use_dynarec && ((port == 0xeb) || (port == 0xed)))
            update_tsc();
#endif
    }

    if (port == 0x0080)
        post_code = val;

#if 0
    if (port == 0x0c02) {
        if (val)
            mem_set_mem_state(0x000e0000, 0x00020000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        else
            mem_set_mem_state(0x000e0000, 0x00020000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
    }
#endif

    if (1) {
        io_log("[%04X:%08X] (%i, %i, %04i) outw(%04X, %04X)\n", CS, cpu_state.pc, in_smm, found, qfound, port, val);
    }

    return;
}

uint32_t
inl(uint16_t port)
{
    io_t    *p;
    io_t    *q;
    uint32_t ret = 0xffffffff;
    uint16_t ret16[2];
    uint8_t  ret8[4];
    int      found  = 0;
#ifdef ENABLE_IO_LOG
    int      qfound = 0;
#endif

#ifdef USE_DEBUG_REGS_486
    io_debug_check_addr(port);
#endif

    if ((pci_flags & FLAG_CONFIG_IO_ON) && (port >= pci_base) && (port < (pci_base + pci_size))) {
        ret = pci_readl(port, NULL);
        found = 4;
#ifdef ENABLE_IO_LOG
        qfound = 1;
#endif
    } else if ((pci_flags & FLAG_CONFIG_DEV0_IO_ON) && (port >= 0xc000) && (port < 0xc100)) {
        ret = pci_readl(port, NULL);
        found = 4;
#ifdef ENABLE_IO_LOG
        qfound = 1;
#endif
    } else {
        p = io[port];
        while (p) {
            q = p->next;
            if (p->inl) {
                ret &= p->inl(port, p->priv);
                found |= 4;
#ifdef ENABLE_IO_LOG
                qfound++;
#endif
            }
            p = q;
        }

        ret16[0] = ret & 0xffff;
        ret16[1] = (ret >> 16) & 0xffff;
        p        = io[port & 0xffff];
        while (p) {
            q = p->next;
            if (p->inw && !p->inl) {
                ret16[0] &= p->inw(port, p->priv);
                found |= 2;
#ifdef ENABLE_IO_LOG
                qfound++;
#endif
            }
            p = q;
        }

        p = io[(port + 2) & 0xffff];
        while (p) {
            q = p->next;
            if (p->inw && !p->inl) {
                ret16[1] &= p->inw(port + 2, p->priv);
                found |= 2;
#ifdef ENABLE_IO_LOG
                qfound++;
#endif
            }
            p = q;
        }
        ret = (ret16[1] << 16) | ret16[0];

        ret8[0] = ret & 0xff;
        ret8[1] = (ret >> 8) & 0xff;
        ret8[2] = (ret >> 16) & 0xff;
        ret8[3] = (ret >> 24) & 0xff;
        for (uint8_t i = 0; i < 4; i++) {
            p = io[(port + i) & 0xffff];
            while (p) {
                q = p->next;
                if (p->inb && !p->inw && !p->inl) {
                    ret8[i] &= p->inb(port + i, p->priv);
                    found |= 1;
#ifdef ENABLE_IO_LOG
                    qfound++;
#endif
                }
                p = q;
            }
        }
        ret = (ret8[3] << 24) | (ret8[2] << 16) | (ret8[1] << 8) | ret8[0];
    }

    if (amstrad_latch & 0x80000000) {
        if (port & 0x80)
            amstrad_latch = AMSTRAD_NOLATCH | 0x80000000;
        else if (port & 0x4000)
            amstrad_latch = AMSTRAD_SW10 | 0x80000000;
        else
            amstrad_latch = AMSTRAD_SW9 | 0x80000000;
    }

    if (!found)
        cycles -= io_delay;

    if (1) {
        io_log("[%04X:%08X] (%i, %i, %04i) in l(%04X) = %08X\n", CS, cpu_state.pc, in_smm, found, qfound, port, ret);
    }

    return ret;
}

void
outl(uint16_t port, uint32_t val)
{
    io_t *p;
    io_t *q;
    int   found  = 0;
#ifdef ENABLE_IO_LOG
    int   qfound = 0;
#endif
    int   i      = 0;

#ifdef USE_DEBUG_REGS_486
    io_debug_check_addr(port);
#endif

    if ((pci_flags & FLAG_CONFIG_IO_ON) && (port >= pci_base) && (port < (pci_base + pci_size))) {
        pci_writel(port, val, NULL);
        found = 4;
#ifdef ENABLE_IO_LOG
        qfound = 1;
#endif
    } else if ((pci_flags & FLAG_CONFIG_DEV0_IO_ON) && (port >= 0xc000) && (port < 0xc100)) {
        pci_writel(port, val, NULL);
        found = 4;
#ifdef ENABLE_IO_LOG
        qfound = 1;
#endif
    } else {
        p = io[port];
        if (p) {
            while (p) {
                q = p->next;
                if (p->outl) {
                    p->outl(port, val, p->priv);
                    found |= 4;
#ifdef ENABLE_IO_LOG
                    qfound++;
#endif
                }
                p = q;
            }
        }

        for (i = 0; i < 4; i += 2) {
            p = io[(port + i) & 0xffff];
            while (p) {
                q = p->next;
                if (p->outw && !p->outl) {
                    p->outw(port + i, val >> (i << 3), p->priv);
                    found |= 2;
#ifdef ENABLE_IO_LOG
                    qfound++;
#endif
                }
                p = q;
            }
        }

        for (i = 0; i < 4; i++) {
            p = io[(port + i) & 0xffff];
            while (p) {
                q = p->next;
                if (p->outb && !p->outw && !p->outl) {
                    p->outb(port + i, val >> (i << 3), p->priv);
                    found |= 1;
#ifdef ENABLE_IO_LOG
                    qfound++;
#endif
                }
                p = q;
            }
        }
    }

    if (!found) {
        cycles -= io_delay;
#ifdef USE_DYNAREC
        if (cpu_use_dynarec && ((port == 0xeb) || (port == 0xed)))
            update_tsc();
#endif
    }

    if (1) {
        io_log("[%04X:%08X] (%i, %i, %04i) outl(%04X, %08X)\n", CS, cpu_state.pc, in_smm, found, qfound, port, val);
    }

    return;
}

static uint8_t
io_trap_readb(uint16_t addr, void *priv)
{
    io_trap_t *trap = (io_trap_t *) priv;
    trap->func(1, addr, 0, 0, trap->priv);
    pclog("[%04X:%08X] io_trap_readb(%04X)\n", CS, cpu_state.pc, addr);
    return 0xff;
}

static uint16_t
io_trap_readw(uint16_t addr, void *priv)
{
    io_trap_t *trap = (io_trap_t *) priv;
    trap->func(2, addr, 0, 0, trap->priv);
    pclog("[%04X:%08X] io_trap_readw(%04X)\n", CS, cpu_state.pc, addr);
    return 0xffff;
}

static uint32_t
io_trap_readl(uint16_t addr, void *priv)
{
    io_trap_t *trap = (io_trap_t *) priv;
    trap->func(4, addr, 0, 0, trap->priv);
    pclog("[%04X:%08X] io_trap_readl(%04X)\n", CS, cpu_state.pc, addr);
    return 0xffffffff;
}

static void
io_trap_writeb(uint16_t addr, uint8_t val, void *priv)
{
    io_trap_t *trap = (io_trap_t *) priv;
    trap->func(1, addr, 1, val, trap->priv);
    pclog("[%04X:%08X] io_trap_writeb(%04X)\n", CS, cpu_state.pc, addr);
}

static void
io_trap_writew(uint16_t addr, uint16_t val, void *priv)
{
    io_trap_t *trap = (io_trap_t *) priv;
    trap->func(2, addr, 1, val, trap->priv);
    pclog("[%04X:%08X] io_trap_writew(%04X)\n", CS, cpu_state.pc, addr);
}

static void
io_trap_writel(uint16_t addr, uint32_t val, void *priv)
{
    io_trap_t *trap = (io_trap_t *) priv;
    trap->func(4, addr, 1, val, trap->priv);
    pclog("[%04X:%08X] io_trap_writel(%04X)\n", CS, cpu_state.pc, addr);
}

void *
io_trap_add(void (*func)(int size, uint16_t addr, uint8_t write, uint8_t val, void *priv),
            void *priv)
{
    /* Instantiate new I/O trap. */
    io_trap_t *trap = (io_trap_t *) malloc(sizeof(io_trap_t));
    trap->enable    = 0;
    trap->base = trap->size = 0;
    trap->func              = func;
    trap->priv              = priv;

    return trap;
}

void
io_trap_remap(void *handle, int enable, uint16_t addr, uint16_t size)
{
    io_trap_t *trap = (io_trap_t *) handle;
    if (!trap)
        return;

    io_log("I/O: Remapping trap from %04X-%04X (enable %d) to %04X-%04X (enable %d)\n",
           trap->base, trap->base + trap->size - 1, trap->enable, addr, addr + size - 1, enable);

    /* Remove old I/O mapping. */
    if (trap->enable && trap->size) {
        io_removehandler(trap->base, trap->size,
                         io_trap_readb, io_trap_readw, io_trap_readl,
                         io_trap_writeb, io_trap_writew, io_trap_writel,
                         trap);
    }

    /* Set trap enable flag, base address and size. */
    trap->enable = !!enable;
    trap->base   = addr;
    trap->size   = size;

    /* Add new I/O mapping. */
    if (trap->enable && trap->size) {
        io_sethandler(trap->base, trap->size,
                      io_trap_readb, io_trap_readw, io_trap_readl,
                      io_trap_writeb, io_trap_writew, io_trap_writel,
                      trap);
    }

    if ((addr == 0x0170) || (addr == 0x0376) || (addr == 0x01f0) || (addr == 0x03f6))
        pclog("io_trap_remape(%04X) = %i\n", addr, trap->enable);
}

void
io_trap_remove(void *handle)
{
    io_trap_t *trap = (io_trap_t *) handle;
    if (!trap)
        return;

    /* Unmap I/O trap before freeing it. */
    io_trap_remap(trap, 0, 0, 0);

    free(trap);
}
