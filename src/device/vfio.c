/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Virtual Function I/O PCI passthrough handler.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2021-2025 RichardG.
 */
#define _FILE_OFFSET_BITS   64
#define _LARGEFILE64_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/vfio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#define HAVE_STDARG_H
#include "cpu.h"
#include <86box/86box.h>
#include <86box/ini.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/i2c.h> /* log2i */
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/path.h>
#include <86box/pci.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/video.h>

/* Just so we don't have to include Linux's pci.h, which
   has some defines that conflict with our own pci.h */
#define PCI_SLOT(devfn) (((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn) ((devfn) & 0x07)

enum {
    NVIDIA_3D0_NONE = 0,
    NVIDIA_3D0_SELECT,
    NVIDIA_3D0_WINDOW,
    NVIDIA_3D0_READ,
    NVIDIA_3D0_WRITE
};

typedef struct {
    int                   fd;
    uint64_t              precalc_offset;
    uint64_t              offset;
    uint64_t              size;
    uint32_t              emulated_offset;
    uint8_t              *mmap_base;
    uint8_t              *mmap_precalc;
    uint8_t               type;
    uint8_t               bar_id;
    uint8_t               read  : 1;
    uint8_t               write : 1;
    mem_mapping_t         mem_mapping;
    char                  name[20];
    struct _vfio_device_ *dev;

    struct {
        mem_mapping_t mem_mappings[2];

        struct {
            uint32_t offset;
        } iomirror;

        struct {
            uint32_t offset;
        } configmirror;

        struct {
            struct {
                uint32_t start;
                uint32_t end;
            } offset[2];
            uint32_t index;
        } configwindow;
    } quirks;
} vfio_region_t;

typedef struct {
    struct _vfio_device_ *dev;
    int                   fd;
    int                   type;
    int                   vector;
    uint16_t              msix_offset;
} vfio_irq_t;

typedef struct _vfio_device_ {
    int      fd;
    uint8_t  mem_enabled   : 1;
    uint8_t  io_enabled    : 1;
    uint8_t  rom_enabled   : 1;
    uint8_t  can_reset     : 1;
    uint8_t  can_flr_reset : 1;
    uint8_t  can_pm_reset  : 1;
    uint8_t  can_hot_reset : 1;
    uint8_t  slot;
    uint8_t  bar_count;
    uint8_t  pm_cap;
    uint8_t  msi_cap;
    uint8_t  msix_cap;
    uint8_t  pcie_cap;
    uint8_t  af_cap;
    char    *name;
    char    *rom_fn;

    vfio_region_t bars[6];
    vfio_region_t rom;
    vfio_region_t config;
    vfio_region_t vga_io_lo;
    vfio_region_t vga_io_hi;
    vfio_region_t vga_mem;

    struct {
        uint8_t     type;
        int         vector_count;
        vfio_irq_t *vectors;

        struct {
            int     raised;
            uint8_t pin;
            uint8_t state;
        } intx;
        struct {
            uint32_t address;
            uint32_t address_upper;
            uint32_t pending;
            uint32_t mask;
            uint16_t ctl;
            uint16_t data;
            uint16_t vector_enable_mask;
            uint8_t  vector_count;
            uint8_t  vector_enable_count;
        } msi;
        struct {
            mem_mapping_t table_mapping;
            mem_mapping_t pba_mapping;
            uint32_t      table_offset;
            uint32_t      pba_offset;
            uint32_t      table_offset_precalc;
            uint32_t      pba_offset_precalc;
            uint16_t      ctl;
            uint16_t      vector_count;
            uint16_t      table_size;
            uint16_t      pba_size;
            uint8_t       table_bar;
            uint8_t       pba_bar;
            uint8_t      *table;
            uint8_t      *pba;
        } msix;
    } irq;

    struct {
        union {
            struct {
                vfio_region_t *bar;
            } ati3c3;

            struct {
                uint64_t master_enable;
                uint8_t  bar_enable;
            } nvidiabar5;

            struct {
                uint32_t index;
                uint8_t  state;
            } nvidia3d0;
        };
    } quirks;

    struct _vfio_device_ *next;
} vfio_device_t;

typedef struct _vfio_group_ {
    int id;
    int fd;

    vfio_device_t *first_device;
    vfio_device_t *current_device;

    struct _vfio_group_ *next;
} vfio_group_t;

static video_timings_t timing_default     = { VIDEO_PCI, 8, 16, 32, 8, 16, 32 };
static int             container_fd       = -1;
static int             epoll_fd           = -1;
static int             irq_thread_wake_fd = -1;
static int             closing            = 0;
static int             intx_high          = 0;
static int             timing_readb       = 0;
static int             timing_readw       = 0;
static int             timing_readl       = 0;
static int             timing_writeb      = 0;
static int             timing_writew      = 0;
static int             timing_writel      = 0;
static vfio_group_t   *first_group        = NULL;
static vfio_group_t   *current_group;
static thread_t       *irq_thread;
static event_t        *irq_event;
static event_t        *irq_thread_resume;
static pc_timer_t      irq_timer;
static vfio_irq_t     *current_irq = NULL;
static const device_t  vfio_device;

#define ENABLE_VFIO_LOG 2
#ifdef ENABLE_VFIO_LOG
int vfio_do_log = ENABLE_VFIO_LOG;

static void
vfio_log(const char *fmt, ...)
{
    va_list ap;

    if (vfio_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}

#    if ENABLE_VFIO_LOG == 2
#        define vfio_log_op vfio_log
#    else
#        define vfio_log_op(fmt, ...)
#    endif
#else
#    define vfio_log(fmt, ...)
#    define vfio_log_op(fmt, ...)
#endif

static uint8_t  vfio_bar_gettype(vfio_device_t *dev, vfio_region_t *bar);
static uint8_t  vfio_config_readb(int func, int addr, int len, void *priv);
static uint16_t vfio_config_readw(int func, int addr, int len, void *priv);
static uint32_t vfio_config_readl(int func, int addr, int len, void *priv);
static void     vfio_config_writeb(int func, int addr, int len, uint8_t val, void *priv);
static void     vfio_config_writew(int func, int addr, int len, uint16_t val, void *priv);
static void     vfio_config_writel(int func, int addr, int len, uint32_t val, void *priv);
static void     vfio_irq_intx_setpin(vfio_device_t *dev);
static void     vfio_irq_msi_disable(vfio_device_t *dev);
static void     vfio_irq_msix_disable(vfio_device_t *dev);
static void     vfio_irq_msix_updatemask(vfio_device_t *dev, uint16_t offset);
static void     vfio_irq_enable(vfio_device_t *dev, int type);

#define VFIO_RW(space, length_char, addr_type, addr_slength, val_type, val_slength)                                                                     \
    static val_type                                                                                                                                     \
    vfio_##space##_read##length_char##_fd(addr_type addr, void *priv)                                                                                   \
    {                                                                                                                                                   \
        register vfio_region_t *region = (vfio_region_t *) priv;                                                                                        \
        val_type                ret;                                                                                                                    \
        if (pread(region->fd, &ret, sizeof(ret), region->precalc_offset + addr) != sizeof(ret))                                                         \
            ret = -1;                                                                                                                                   \
        vfio_log_op("[%04X:%08X] VFIO: " #space "_read" #length_char "_fd(%0" #addr_slength "X) = %0" #val_slength "X\n", CS, cpu_state.pc, addr, ret); \
        cycles -= timing_read##length_char;                                                                                                             \
        intx_high = 0;                                                                                                                                  \
        return ret;                                                                                                                                     \
    }                                                                                                                                                   \
                                                                                                                                                        \
    static void                                                                                                                                         \
    vfio_##space##_write##length_char##_fd(addr_type addr, val_type val, void *priv)                                                                    \
    {                                                                                                                                                   \
        register vfio_region_t *region = (vfio_region_t *) priv;                                                                                        \
        vfio_log_op("[%04X:%08X] VFIO: " #space "_write" #length_char "_fd(%0" #addr_slength "X, %0" #val_slength "X)\n", CS, cpu_state.pc, addr, val); \
        (void) !pwrite(region->fd, &val, sizeof(val), region->precalc_offset + addr);                                                                  \
        cycles -= timing_write##length_char;                                                                                                            \
        intx_high = 0;                                                                                                                                  \
    }                                                                                                                                                   \
                                                                                                                                                        \
    static val_type                                                                                                                                     \
    vfio_##space##_read##length_char##_mm(addr_type addr, void *priv)                                                                                   \
    {                                                                                                                                                   \
        register val_type ret = *((val_type *) &((uint8_t *) priv)[addr]);                                                                              \
        vfio_log_op("[%04X:%08X] VFIO: " #space "_read" #length_char "_mm(%0" #addr_slength "X) = %0" #val_slength "X\n", CS, cpu_state.pc, addr, ret); \
        cycles -= timing_read##length_char;                                                                                                             \
        intx_high = 0;                                                                                                                                  \
        return ret;                                                                                                                                     \
    }                                                                                                                                                   \
                                                                                                                                                        \
    static void                                                                                                                                         \
    vfio_##space##_write##length_char##_mm(addr_type addr, val_type val, void *priv)                                                                    \
    {                                                                                                                                                   \
        vfio_log_op("[%04X:%08X] VFIO: " #space "_write" #length_char "_mm(%0" #addr_slength "X, %0" #val_slength "X)\n", CS, cpu_state.pc, addr, val); \
        *((val_type *) &((uint8_t *) priv)[addr]) = val;                                                                                                \
        cycles -= timing_write##length_char;                                                                                                            \
        intx_high = 0;                                                                                                                                  \
    }

VFIO_RW(mem, b, uint32_t, 8, uint8_t, 2)
VFIO_RW(mem, w, uint32_t, 8, uint16_t, 4)
VFIO_RW(mem, l, uint32_t, 8, uint32_t, 8)
VFIO_RW(io, b, uint16_t, 4, uint8_t, 2)
VFIO_RW(io, w, uint16_t, 4, uint16_t, 4)
VFIO_RW(io, l, uint16_t, 4, uint32_t, 8)

static void
vfio_quirk_capture_io(vfio_device_t *dev, vfio_region_t *bar,
                      uint16_t base, uint16_t size, uint8_t enable,
                      uint8_t (*inb)(uint16_t addr, void *priv),
                      uint16_t (*inw)(uint16_t addr, void *priv),
                      uint32_t (*inl)(uint16_t addr, void *priv),
                      void (*outb)(uint16_t addr, uint8_t val, void *priv),
                      void (*outw)(uint16_t addr, uint16_t val, void *priv),
                      void (*outl)(uint16_t addr, uint32_t val, void *priv))
{
    /* Remove quirk handler from port range. */
    io_removehandler(base, size,
                     bar->read ? inb : NULL,
                     bar->read ? inw : NULL,
                     bar->read ? inl : NULL,
                     bar->write ? outb : NULL,
                     bar->write ? outw : NULL,
                     bar->write ? outl : NULL,
                     dev ? ((void *) dev) : ((void *) bar));

    if (enable) {
        /* Remove existing handler from port range. */
        if (bar->mmap_base) /* mmap available */
            io_removehandler(base, size,
                             bar->read ? vfio_io_readb_mm : NULL,
                             bar->read ? vfio_io_readw_mm : NULL,
                             bar->read ? vfio_io_readl_mm : NULL,
                             bar->write ? vfio_io_writeb_mm : NULL,
                             bar->write ? vfio_io_writew_mm : NULL,
                             bar->write ? vfio_io_writel_mm : NULL,
                             bar->mmap_precalc);
        else /* mmap not available */
            io_removehandler(base, size,
                             bar->read ? vfio_io_readb_fd : NULL,
                             bar->read ? vfio_io_readw_fd : NULL,
                             bar->read ? vfio_io_readl_fd : NULL,
                             bar->write ? vfio_io_writeb_fd : NULL,
                             bar->write ? vfio_io_writew_fd : NULL,
                             bar->write ? vfio_io_writel_fd : NULL,
                             bar);

        /* Add quirk handler to port range. */
        io_sethandler(base, size,
                      bar->read ? inb : NULL,
                      bar->read ? inw : NULL,
                      bar->read ? inl : NULL,
                      bar->write ? outb : NULL,
                      bar->write ? outw : NULL,
                      bar->write ? outl : NULL,
                      dev ? ((void *) dev) : ((void *) bar));
    }
}

static uint8_t
vfio_quirk_configmirror_readb(uint32_t addr, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;
    vfio_device_t *dev = bar->dev;

    /* Cascade to the main handler. */
    vfio_mem_readb_fd(addr, bar);

    /* Read configuration register. */
    uint8_t ret = vfio_config_readb(0, addr - bar->quirks.configmirror.offset, 1, dev);
    vfio_log_op("VFIO %s: Config mirror: Read %02X from index %02X\n",
                dev->name, ret, addr - bar->quirks.configmirror.offset);

    return ret;
}

static uint16_t
vfio_quirk_configmirror_readw(uint32_t addr, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;
    vfio_device_t *dev = bar->dev;

    /* Cascade to the main handler. */
    vfio_mem_readw_fd(addr, bar);

    /* Read configuration register. */
    uint16_t ret = vfio_config_readw(0, addr - bar->quirks.configmirror.offset, 2, dev);
    vfio_log_op("VFIO %s: Config mirror: Read %04X from index %02X\n",
                dev->name, ret, addr - bar->quirks.configmirror.offset);

    return ret;
}

static uint32_t
vfio_quirk_configmirror_readl(uint32_t addr, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;
    vfio_device_t *dev = bar->dev;

    /* Cascade to the main handler. */
    vfio_mem_readl_fd(addr, bar);

    /* Read configuration register. */
    uint32_t ret = vfio_config_readl(0, addr - bar->quirks.configmirror.offset, 4, dev);
    vfio_log_op("VFIO %s: Config mirror: Read %08X from index %02X\n",
                dev->name, ret, addr - bar->quirks.configmirror.offset);

    return ret;
}

static void
vfio_quirk_configmirror_writeb(uint32_t addr, uint8_t val, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;
    vfio_device_t *dev = bar->dev;

    /* Write configuration register. */
    vfio_log_op("VFIO %s: Config mirror: Write %02X to index %02X\n",
                dev->name, val, addr - bar->quirks.configmirror.offset);
    vfio_config_writeb(0, addr - bar->quirks.configmirror.offset, 1, val, dev);
}

static void
vfio_quirk_configmirror_writew(uint32_t addr, uint16_t val, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;
    vfio_device_t *dev = bar->dev;

    /* Write configuration register. */
    vfio_log_op("VFIO %s: Config mirror: Write %04X to index %02X\n",
                dev->name, val, addr - bar->quirks.configmirror.offset);
    vfio_config_writew(0, addr - bar->quirks.configmirror.offset, 2, val, dev);
}

static void
vfio_quirk_configmirror_writel(uint32_t addr, uint32_t val, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;
    vfio_device_t *dev = bar->dev;

    /* Write configuration register. */
    vfio_log_op("VFIO %s: Config mirror: Write %08X to index %02X\n",
                dev->name, val, addr - bar->quirks.configmirror.offset);
    vfio_config_writel(0, addr - bar->quirks.configmirror.offset, 4, val, dev);
}

static void
vfio_quirk_configmirror(vfio_device_t *dev, vfio_region_t *bar,
                        uint32_t offset, uint8_t mapping_slot, uint8_t enable)
{
    /* Get the additional memory mapping structure. */
    mem_mapping_t *mapping = &bar->quirks.mem_mappings[mapping_slot];

    vfio_log("VFIO %s: %sapping configuration space mirror for %s @ %08X\n",
             dev->name, enable ? "M" : "Unm", bar->name, bar->emulated_offset + offset);

    /* Add mapping if it wasn't already added.
       Being added after region setup, it should override the main BAR mapping. */
    if (!mapping->base)
        mem_mapping_add(mapping, 0, 0,
                        vfio_quirk_configmirror_readb,
                        vfio_quirk_configmirror_readw,
                        vfio_quirk_configmirror_readl,
                        vfio_quirk_configmirror_writeb,
                        vfio_quirk_configmirror_writew,
                        vfio_quirk_configmirror_writel,
                        NULL, MEM_MAPPING_EXTERNAL, bar);

    /* Store start offset. */
    bar->quirks.configmirror.offset = bar->emulated_offset + offset;

    /* Enable or disable mapping. */
    if (enable)
        mem_mapping_set_addr(mapping, bar->emulated_offset + offset, 256);
    else
        mem_mapping_disable(mapping);
}

static void
vfio_quirk_configwindow_index_writeb(uint16_t addr, uint8_t val, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;
    vfio_device_t *dev = bar->dev;

    /* Write configuration register index. */
    vfio_log_op("VFIO %s: Config window: Write index[%d] %02X\n",
                dev->name, addr & 3, val);
    uint8_t offset = (addr & 3) << 3;
    bar->quirks.configwindow.index &= ~(0x000000ff << offset);
    bar->quirks.configwindow.index |= val << offset;

    /* Cascade to the main handler. */
    vfio_io_writeb_fd(addr, val, bar);
}

static void
vfio_quirk_configwindow_index_writew(uint16_t addr, uint16_t val, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;
    vfio_device_t *dev = bar->dev;

    /* Write configuration register index. */
    vfio_log_op("VFIO %s: Config window: Write index[%d] %04X\n",
                dev->name, addr & 2, val);
    uint8_t offset = (addr & 2) << 3;
    bar->quirks.configwindow.index &= ~(0x0000ffff << offset);
    bar->quirks.configwindow.index |= val << offset;

    /* Cascade to the main handler. */
    vfio_io_writew_fd(addr, val, bar);
}

static void
vfio_quirk_configwindow_index_writel(uint16_t addr, uint32_t val, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;
    vfio_device_t *dev = bar->dev;

    /* Write configuration register index. */
    vfio_log_op("VFIO %s: Config window: Write index %08X\n",
                dev->name, val);
    bar->quirks.configwindow.index = val;

    /* Cascade to the main handler. */
    vfio_io_writel_fd(addr, val, bar);
}

static uint8_t
vfio_quirk_configwindow_data_readb(uint16_t addr, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;
    vfio_device_t *dev = bar->dev;

    /* Cascade to the main handler. */
    uint8_t ret = vfio_io_readb_fd(addr, bar);

    /* Read configuration register if part of the main PCI configuration space. */
    uint32_t index = bar->quirks.configwindow.index;
    if ((index >= bar->quirks.configwindow.offset[0].start) && (index <= bar->quirks.configwindow.offset[0].end)) {
        ret = vfio_config_readb(0, index - bar->quirks.configwindow.offset[0].start, 1, dev);
        vfio_log_op("VFIO %s: Config window: Read %02X from primary index %08X\n",
                    dev->name, ret, index);
    } else if ((index >= bar->quirks.configwindow.offset[1].start) && (index <= bar->quirks.configwindow.offset[1].end)) {
        ret = vfio_config_readb(0, index - bar->quirks.configwindow.offset[1].start, 1, dev);
        vfio_log_op("VFIO %s: Config window: Read %02X from secondary index %08X\n",
                    dev->name, ret, index);
    }

    return ret;
}

static uint16_t
vfio_quirk_configwindow_data_readw(uint16_t addr, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;
    vfio_device_t *dev = bar->dev;

    /* Cascade to the main handler. */
    uint16_t ret = vfio_io_readw_fd(addr, bar);

    /* Read configuration register if part of the main PCI configuration space. */
    uint32_t index = bar->quirks.configwindow.index;
    if ((index >= bar->quirks.configwindow.offset[0].start) && (index <= bar->quirks.configwindow.offset[0].end)) {
        ret = vfio_config_readw(0, index - bar->quirks.configwindow.offset[0].start, 2, dev);
        vfio_log_op("VFIO %s: Config window: Read %04X from primary index %08X\n",
                    dev->name, ret, index);
    } else if ((index >= bar->quirks.configwindow.offset[1].start) && (index <= bar->quirks.configwindow.offset[1].end)) {
        ret = vfio_config_readw(0, index - bar->quirks.configwindow.offset[1].start, 2, dev);
        vfio_log_op("VFIO %s: Config window: Read %04X from secondary index %08X\n",
                    dev->name, ret, index);
    }

    return ret;
}

static uint32_t
vfio_quirk_configwindow_data_readl(uint16_t addr, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;
    vfio_device_t *dev = bar->dev;

    /* Cascade to the main handler. */
    uint32_t ret = vfio_io_readl_fd(addr, bar);

    /* Read configuration register if part of the main PCI configuration space. */
    uint32_t index = bar->quirks.configwindow.index;
    if ((index >= bar->quirks.configwindow.offset[0].start) && (index <= bar->quirks.configwindow.offset[0].end)) {
        ret = vfio_config_readl(0, index - bar->quirks.configwindow.offset[0].start, 4, dev);
        vfio_log_op("VFIO %s: Config window: Read %08X from primary index %08X\n",
                    dev->name, ret, index);
    } else if ((index >= bar->quirks.configwindow.offset[1].start) && (index <= bar->quirks.configwindow.offset[1].end)) {
        ret = vfio_config_readl(0, index - bar->quirks.configwindow.offset[1].start, 4, dev);
        vfio_log_op("VFIO %s: Config window: Read %08X from secondary index %08X\n",
                    dev->name, ret, index);
    }

    return ret;
}

static void
vfio_quirk_configwindow_data_writeb(uint16_t addr, uint8_t val, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;
    vfio_device_t *dev = bar->dev;

    /* Write configuration register if part of the main PCI configuration space. */
    uint32_t index = bar->quirks.configwindow.index;
    if ((index >= bar->quirks.configwindow.offset[0].start) && (index <= bar->quirks.configwindow.offset[0].end)) {
        vfio_log_op("VFIO %s: Config window: Write %02X to primary index %08X\n",
                    dev->name, val, index);
        vfio_config_writeb(0, index - bar->quirks.configwindow.offset[0].start, 1, val, dev);
        return;
    } else if ((index >= bar->quirks.configwindow.offset[1].start) && (index <= bar->quirks.configwindow.offset[1].end)) {
        vfio_log_op("VFIO %s: Config window: Write %02X to secondary index %08X\n",
                    dev->name, val, index);
        vfio_config_writeb(0, index - bar->quirks.configwindow.offset[1].start, 1, val, dev);
        return;
    }

    /* Cascade to the main handler. */
    vfio_io_writeb_fd(addr, val, bar);
}

static void
vfio_quirk_configwindow_data_writew(uint16_t addr, uint16_t val, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;
    vfio_device_t *dev = bar->dev;

    /* Write configuration register if part of the main PCI configuration space. */
    uint32_t index = bar->quirks.configwindow.index;
    if ((index >= bar->quirks.configwindow.offset[0].start) && (index <= bar->quirks.configwindow.offset[0].end)) {
        vfio_log_op("VFIO %s: Config window: Write %04X to primary index %08X\n",
                    dev->name, val, index);
        vfio_config_writew(0, index - bar->quirks.configwindow.offset[0].start, 2, val, dev);
        return;
    } else if ((index >= bar->quirks.configwindow.offset[1].start) && (index <= bar->quirks.configwindow.offset[1].end)) {
        vfio_log_op("VFIO %s: Config window: Write %04X to secondary index %08X\n",
                    dev->name, val, index);
        vfio_config_writew(0, index - bar->quirks.configwindow.offset[1].start, 2, val, dev);
        return;
    }

    /* Cascade to the main handler. */
    vfio_io_writew_fd(addr, val, bar);
}

static void
vfio_quirk_configwindow_data_writel(uint16_t addr, uint32_t val, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;
    vfio_device_t *dev = bar->dev;

    /* Write configuration register if part of the main PCI configuration space. */
    uint32_t index = bar->quirks.configwindow.index;
    if ((index >= bar->quirks.configwindow.offset[0].start) && (index <= bar->quirks.configwindow.offset[0].end)) {
        vfio_log_op("VFIO %s: Config window: Write %08X to primary index %08X\n",
                    dev->name, val, index);
        vfio_config_writel(0, index - bar->quirks.configwindow.offset[0].start, 4, val, dev);
        return;
    } else if ((index >= bar->quirks.configwindow.offset[1].start) && (index <= bar->quirks.configwindow.offset[1].end)) {
        vfio_log_op("VFIO %s: Config window: Write %08X to secondary index %08X\n",
                    dev->name, val, index);
        vfio_config_writel(0, index - bar->quirks.configwindow.offset[1].start, 4, val, dev);
        return;
    }

    /* Cascade to the main handler. */
    vfio_io_writel_fd(addr, val, bar);
}

static void
vfio_quirk_configwindow(vfio_device_t *dev, vfio_region_t *bar,
                        uint16_t index_offset, uint16_t index_size,
                        uint16_t data_offset, uint16_t data_size,
                        uint32_t window_offset0, uint32_t window_offset1, uint8_t enable)
{
    vfio_log("VFIO %s: %sapping configuration space window for %s @ %04X and %04X\n",
             dev->name, enable ? "M" : "Unm", bar->name,
             bar->emulated_offset + index_offset, bar->emulated_offset + data_offset);

    /* Store start offsets, as well as end offsets to speed up operations. */
    bar->quirks.configwindow.offset[0].start = window_offset0;
    bar->quirks.configwindow.offset[0].end   = window_offset0 + 255;
    bar->quirks.configwindow.offset[1].start = window_offset1;
    bar->quirks.configwindow.offset[1].end   = window_offset1 + 255;

    /* Enable or disable mapping. */
    vfio_quirk_capture_io(NULL, bar, bar->emulated_offset + index_offset, index_size, enable,
                          vfio_io_readb_fd,
                          vfio_io_readw_fd,
                          vfio_io_readl_fd,
                          vfio_quirk_configwindow_index_writeb,
                          vfio_quirk_configwindow_index_writew,
                          vfio_quirk_configwindow_index_writel);
    vfio_quirk_capture_io(NULL, bar, bar->emulated_offset + data_offset, data_size, enable,
                          vfio_quirk_configwindow_data_readb,
                          vfio_quirk_configwindow_data_readw,
                          vfio_quirk_configwindow_data_readl,
                          vfio_quirk_configwindow_data_writeb,
                          vfio_quirk_configwindow_data_writew,
                          vfio_quirk_configwindow_data_writel);
}

static uint8_t
vfio_quirk_iomirror_readb(uint16_t addr, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;

    /* Read I/O port mirror from memory-mapped space. */
    uint8_t ret = vfio_mem_readb_fd(bar->emulated_offset + bar->quirks.iomirror.offset + addr, bar);
#ifdef ENABLE_VFIO_LOG
    vfio_device_t *dev = bar->dev;
    vfio_log_op("VFIO %s: I/O mirror: Read %02X from %04X (%08X)\n", dev->name,
                ret, addr, bar->quirks.iomirror.offset + addr);
#endif
    return ret;
}

static uint16_t
vfio_quirk_iomirror_readw(uint16_t addr, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;

    /* Read I/O port mirror from memory-mapped space. */
    uint16_t ret = vfio_mem_readw_fd(bar->emulated_offset + bar->quirks.iomirror.offset + addr, bar);
#ifdef ENABLE_VFIO_LOG
    vfio_device_t *dev = bar->dev;
    vfio_log_op("VFIO %s: I/O mirror: Read %04X from %04X (%08X)\n", dev->name,
                ret, addr, bar->quirks.iomirror.offset + addr);
#endif
    return ret;
}

static uint32_t
vfio_quirk_iomirror_readl(uint16_t addr, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;

    /* Read I/O port mirror from memory-mapped space. */
    uint32_t ret = vfio_mem_readl_fd(bar->emulated_offset + bar->quirks.iomirror.offset + addr, bar);
#ifdef ENABLE_VFIO_LOG
    vfio_device_t *dev = bar->dev;
    vfio_log_op("VFIO %s: I/O mirror: Read %08X from %04X (%08X)\n", dev->name,
                ret, addr, bar->quirks.iomirror.offset + addr);
#endif
    return ret;
}

static void
vfio_quirk_iomirror_writeb(uint16_t addr, uint8_t val, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;

    /* Write I/O port mirror to memory-mapped space. */
#ifdef ENABLE_VFIO_LOG
    vfio_device_t *dev = bar->dev;
    vfio_log_op("VFIO %s: I/O mirror: Write %02X to %04X (%08X)\n", dev->name,
                val, addr, bar->quirks.iomirror.offset + addr);
#endif
    vfio_mem_writeb_fd(bar->emulated_offset + bar->quirks.iomirror.offset + addr, val, bar);
}

static void
vfio_quirk_iomirror_writew(uint16_t addr, uint16_t val, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;

    /* Write I/O port mirror to memory-mapped space. */
#ifdef ENABLE_VFIO_LOG
    vfio_device_t *dev = bar->dev;
    vfio_log_op("VFIO %s: I/O mirror: Write %04X to %04X (%08X)\n", dev->name,
                val, addr, bar->quirks.iomirror.offset + addr);
#endif
    vfio_mem_writew_fd(bar->emulated_offset + bar->quirks.iomirror.offset + addr, val, bar);
}

static void
vfio_quirk_iomirror_writel(uint16_t addr, uint32_t val, void *priv)
{
    vfio_region_t *bar = (vfio_region_t *) priv;

    /* Write I/O port mirror to memory-mapped space. */
#ifdef ENABLE_VFIO_LOG
    vfio_device_t *dev = bar->dev;
    vfio_log_op("VFIO %s: I/O mirror: Write %08X to %04X (%08X)\n", dev->name,
                val, addr, bar->quirks.iomirror.offset + addr);
#endif
    vfio_mem_writel_fd(bar->emulated_offset + bar->quirks.iomirror.offset + addr, val, bar);
}

static void
vfio_quirk_iomirror(vfio_device_t *dev, vfio_region_t *bar,
                    uint32_t offset, uint16_t base, uint16_t length, uint8_t enable)
{
    vfio_log("VFIO %s: %sapping I/O mirror for %s @ %08X\n",
             dev->name, enable ? "M" : "Unm", bar->name, bar->emulated_offset + offset);

    /* Save I/O mirror offset, only one per BAR for now. */
    bar->quirks.iomirror.offset = offset;

    /* Add or remove quirk handler from port range. */
    if (enable)
        io_sethandler(base, length,
                      bar->read ? vfio_quirk_iomirror_readb : NULL,
                      bar->read ? vfio_quirk_iomirror_readw : NULL,
                      bar->read ? vfio_quirk_iomirror_readl : NULL,
                      bar->write ? vfio_quirk_iomirror_writeb : NULL,
                      bar->write ? vfio_quirk_iomirror_writew : NULL,
                      bar->write ? vfio_quirk_iomirror_writel : NULL,
                      bar);
    else
        io_removehandler(base, length,
                         bar->read ? vfio_quirk_iomirror_readb : NULL,
                         bar->read ? vfio_quirk_iomirror_readw : NULL,
                         bar->read ? vfio_quirk_iomirror_readl : NULL,
                         bar->write ? vfio_quirk_iomirror_writeb : NULL,
                         bar->write ? vfio_quirk_iomirror_writew : NULL,
                         bar->write ? vfio_quirk_iomirror_writel : NULL,
                         bar);
}

static uint8_t
vfio_quirk_ati3c3_readb(uint16_t addr, void *priv)
{
    vfio_device_t *dev = (vfio_device_t *) priv;

    /* Read high byte of the I/O BAR address. */
    uint8_t ret = dev->quirks.ati3c3.bar->emulated_offset >> 8;
    vfio_log_op("VFIO %s: ATI 3C3: Read %02X\n", ret);

    return ret;
}

static void
vfio_quirk_nvidiabar5(vfio_device_t *dev)
{
    /* Remap config window based on BAR enable status and the master/enable registers. */
    vfio_quirk_configwindow(dev, &dev->bars[5], 0x08, 4, 0x0c, 4, 0x1800, 0x88000,
                            dev->quirks.nvidiabar5.bar_enable && ((dev->quirks.nvidiabar5.master_enable & 0x0000000100000001) == 0x0000000100000001));
}

static void
vfio_quirk_nvidiabar5_writeb(uint16_t addr, uint8_t val, void *priv)
{
    vfio_device_t *dev = (vfio_device_t *) priv;

    /* Write master/enable registers. */
    vfio_log_op("VFIO %s: NVIDIA BAR 5: Write [%d] %02X\n",
                dev->name, addr & 7, val);
    uint8_t offset = (addr & 7) << 3;
    dev->quirks.nvidiabar5.master_enable &= ~(0x00000000000000ff << offset);
    dev->quirks.nvidiabar5.master_enable |= val << offset;

    /* Update window to account for changes in master/enable registers. */
    vfio_quirk_nvidiabar5(dev);

    /* Cascade to the main handler. */
    vfio_io_writeb_fd(addr, val, &dev->bars[5]);
}

static void
vfio_quirk_nvidiabar5_writew(uint16_t addr, uint16_t val, void *priv)
{
    vfio_device_t *dev = (vfio_device_t *) priv;

    /* Write master/enable registers. */
    vfio_log_op("VFIO %s: NVIDIA BAR 5: Write [%d] %04X\n",
                dev->name, addr & 7, val);
    uint8_t offset = (addr & 6) << 3;
    dev->quirks.nvidiabar5.master_enable &= ~(0x000000000000ffff << offset);
    dev->quirks.nvidiabar5.master_enable |= val << offset;

    /* Update window to account for changes in master/enable registers. */
    vfio_quirk_nvidiabar5(dev);

    /* Cascade to the main handler. */
    vfio_io_writew_fd(addr, val, &dev->bars[5]);
}

static void
vfio_quirk_nvidiabar5_writel(uint16_t addr, uint32_t val, void *priv)
{
    vfio_device_t *dev = (vfio_device_t *) priv;

    /* Write master/enable registers. */
    vfio_log_op("VFIO %s: NVIDIA BAR 5: Write [%d] %08X\n",
                dev->name, addr & 7, val);
    uint8_t offset = (addr & 4) << 3;
    dev->quirks.nvidiabar5.master_enable &= ~(0x00000000ffffffff << offset);
    dev->quirks.nvidiabar5.master_enable |= val << offset;

    /* Update window to account for changes in master/enable registers. */
    vfio_quirk_nvidiabar5(dev);

    /* Cascade to the main handler. */
    vfio_io_writel_fd(addr, val, &dev->bars[5]);
}

static uint8_t
vfio_quirk_nvidia3d0_state_readb(uint16_t addr, void *priv)
{
    vfio_device_t *dev = (vfio_device_t *) priv;

    /* Reset state on read. */
    dev->quirks.nvidia3d0.state = NVIDIA_3D0_NONE;
    vfio_log_op("VFIO %s: NVIDIA 3D0: Switching to NONE state (byte read)\n", dev->name);

    /* Cascade to the main handler. */
    return vfio_io_readb_fd(addr, &dev->vga_io_hi);
}

static uint16_t
vfio_quirk_nvidia3d0_state_readw(uint16_t addr, void *priv)
{
    vfio_device_t *dev = (vfio_device_t *) priv;

    /* Reset state on read. */
    dev->quirks.nvidia3d0.state = NVIDIA_3D0_NONE;
    vfio_log_op("VFIO %s: NVIDIA 3D0: Switching to NONE state (word read)\n", dev->name);

    /* Cascade to the main handler. */
    return vfio_io_readw_fd(addr, &dev->vga_io_hi);
}

static uint32_t
vfio_quirk_nvidia3d0_state_readl(uint16_t addr, void *priv)
{
    vfio_device_t *dev = (vfio_device_t *) priv;

    /* Reset state on read. */
    dev->quirks.nvidia3d0.state = NVIDIA_3D0_NONE;
    vfio_log_op("VFIO %s: NVIDIA 3D0: Switching to NONE state (dword read)\n", dev->name);

    /* Cascade to the main handler. */
    return vfio_io_readl_fd(addr, &dev->vga_io_hi);
}

static void
vfio_quirk_nvidia3d0_state_writeb(uint16_t addr, uint8_t val, void *priv)
{
    vfio_device_t *dev = (vfio_device_t *) priv;

    /* Commands don't fit in a byte; just reset state and move on. */
    dev->quirks.nvidia3d0.state = NVIDIA_3D0_NONE;
    vfio_log_op("VFIO %s: NVIDIA 3D0: Switching to NONE state (byte write)\n", dev->name);

    /* Cascade to the main handler. */
    vfio_io_writeb_fd(addr, val, &dev->vga_io_hi);
}

static void
vfio_quirk_nvidia3d0_state_writew(uint16_t addr, uint16_t val, void *priv)
{
    vfio_device_t *dev = (vfio_device_t *) priv;

    uint8_t prev_state          = dev->quirks.nvidia3d0.state;
    dev->quirks.nvidia3d0.state = NVIDIA_3D0_NONE;

    /* Interpret NVIDIA commands. */
    switch (val) {
        case 0x338:
            if (prev_state == NVIDIA_3D0_NONE) {
                dev->quirks.nvidia3d0.state = NVIDIA_3D0_SELECT;
                vfio_log_op("VFIO %s: NVIDIA 3D0: Switching to SELECT state (word write)\n", dev->name);
            }
            break;

        case 0x538:
            if (prev_state == NVIDIA_3D0_WINDOW) {
                dev->quirks.nvidia3d0.state = NVIDIA_3D0_READ;
                vfio_log_op("VFIO %s: NVIDIA 3D0: Switching to READ state (word write)\n", dev->name);
            }
            break;

        case 0x738:
            if (prev_state == NVIDIA_3D0_WINDOW) {
                dev->quirks.nvidia3d0.state = NVIDIA_3D0_WRITE;
                vfio_log_op("VFIO %s: NVIDIA 3D0: Switching to WRITE state (word write)\n", dev->name);
            }
            break;
    }

    /* Cascade to the main handler. */
    vfio_io_writew_fd(addr, val, &dev->vga_io_hi);
}

static void
vfio_quirk_nvidia3d0_state_writel(uint16_t addr, uint32_t val, void *priv)
{
    vfio_device_t *dev = (vfio_device_t *) priv;

    uint8_t prev_state          = dev->quirks.nvidia3d0.state;
    dev->quirks.nvidia3d0.state = NVIDIA_3D0_NONE;

    /* Interpret NVIDIA commands. */
    switch (val) {
        case 0x338:
            if (prev_state == NVIDIA_3D0_NONE) {
                dev->quirks.nvidia3d0.state = NVIDIA_3D0_SELECT;
                vfio_log_op("VFIO %s: NVIDIA 3D0: Switching to SELECT state (dword write)\n", dev->name);
            }
            break;

        case 0x538:
            if (prev_state == NVIDIA_3D0_WINDOW) {
                dev->quirks.nvidia3d0.state = NVIDIA_3D0_READ;
                vfio_log_op("VFIO %s: NVIDIA 3D0: Switching to READ state (dword write)\n", dev->name);
            }
            break;

        case 0x738:
            if (prev_state == NVIDIA_3D0_WINDOW) {
                dev->quirks.nvidia3d0.state = NVIDIA_3D0_WRITE;
                vfio_log_op("VFIO %s: NVIDIA 3D0: Switching to WRITE state (dword write)\n", dev->name);
            }
            break;
    }

    /* Cascade to the main handler. */
    vfio_io_writel_fd(addr, val, &dev->vga_io_hi);
}

static uint8_t
vfio_quirk_nvidia3d0_data_readb(uint16_t addr, void *priv)
{
    vfio_device_t *dev = (vfio_device_t *) priv;

    /* Cascade to the main handler. */
    uint8_t prev_state          = dev->quirks.nvidia3d0.state;
    uint8_t ret                 = vfio_io_readb_fd(addr, &dev->vga_io_hi);
    dev->quirks.nvidia3d0.state = NVIDIA_3D0_NONE;

    /* Read configuration register if part of the main PCI configuration space. */
    if ((prev_state == NVIDIA_3D0_READ) && (((dev->quirks.nvidia3d0.index & 0xffffff00) == 0x00001800) || ((dev->quirks.nvidia3d0.index & 0xffffff00) == 0x00088000))) {
        ret = vfio_config_readb(0, dev->quirks.nvidia3d0.index, 1, dev);
        vfio_log_op("VFIO %s: NVIDIA 3D0: Read %02X from index %08X\n", dev->name,
                    ret, dev->quirks.nvidia3d0.index);
    }

    return ret;
}

static uint16_t
vfio_quirk_nvidia3d0_data_readw(uint16_t addr, void *priv)
{
    vfio_device_t *dev = (vfio_device_t *) priv;

    /* Cascade to the main handler. */
    uint8_t  prev_state         = dev->quirks.nvidia3d0.state;
    uint16_t ret                = vfio_io_readw_fd(addr, &dev->vga_io_hi);
    dev->quirks.nvidia3d0.state = NVIDIA_3D0_NONE;

    /* Read configuration register if part of the main PCI configuration space. */
    if ((prev_state == NVIDIA_3D0_READ) && (((dev->quirks.nvidia3d0.index & 0xffffff00) == 0x00001800) || ((dev->quirks.nvidia3d0.index & 0xffffff00) == 0x00088000))) {
        ret = vfio_config_readw(0, dev->quirks.nvidia3d0.index, 2, dev);
        vfio_log_op("VFIO %s: NVIDIA 3D0: Read %04X from index %08X\n", dev->name,
                    ret, dev->quirks.nvidia3d0.index);
    }

    return ret;
}

static uint32_t
vfio_quirk_nvidia3d0_data_readl(uint16_t addr, void *priv)
{
    vfio_device_t *dev = (vfio_device_t *) priv;

    /* Cascade to the main handler. */
    uint8_t  prev_state         = dev->quirks.nvidia3d0.state;
    uint32_t ret                = vfio_io_readl_fd(addr, &dev->vga_io_hi);
    dev->quirks.nvidia3d0.state = NVIDIA_3D0_NONE;

    /* Read configuration register if part of the main PCI configuration space. */
    if ((prev_state == NVIDIA_3D0_READ) && (((dev->quirks.nvidia3d0.index & 0xffffff00) == 0x00001800) || ((dev->quirks.nvidia3d0.index & 0xffffff00) == 0x00088000))) {
        ret = vfio_config_readl(0, dev->quirks.nvidia3d0.index, 4, dev);
        vfio_log_op("VFIO %s: NVIDIA 3D0: Read %08X from index %08X\n", dev->name,
                    ret, dev->quirks.nvidia3d0.index);
    }

    return ret;
}

static void
vfio_quirk_nvidia3d0_data_writeb(uint16_t addr, uint8_t val, void *priv)
{
    vfio_device_t *dev = (vfio_device_t *) priv;

    uint8_t prev_state          = dev->quirks.nvidia3d0.state;
    dev->quirks.nvidia3d0.state = NVIDIA_3D0_NONE;

    if (prev_state == NVIDIA_3D0_SELECT) {
        /* Write MMIO index. */
        dev->quirks.nvidia3d0.index = val;
        dev->quirks.nvidia3d0.state = NVIDIA_3D0_WINDOW;
        vfio_log_op("VFIO %s: NVIDIA 3D0: Write index %02X\n", dev->name, val);
    } else if (prev_state == NVIDIA_3D0_WRITE) {
        /* Write configuration register if part of the main PCI configuration space. */
        if (((dev->quirks.nvidia3d0.index & 0xffffff00) == 0x00001800) || ((dev->quirks.nvidia3d0.index & 0xffffff00) == 0x00088000)) {
            /* Write configuration register. */
            vfio_log_op("VFIO %s: NVIDIA 3D0: Write %02X to index %08X\n", dev->name,
                        val, dev->quirks.nvidia3d0.index);
            vfio_config_writeb(0, dev->quirks.nvidia3d0.index, val, 1, dev);
            return;
        }
    }

    /* Cascade to the main handler. */
    vfio_io_writeb_fd(addr, val, &dev->vga_io_hi);
}

static void
vfio_quirk_nvidia3d0_data_writew(uint16_t addr, uint16_t val, void *priv)
{
    vfio_device_t *dev = (vfio_device_t *) priv;

    uint8_t prev_state          = dev->quirks.nvidia3d0.state;
    dev->quirks.nvidia3d0.state = NVIDIA_3D0_NONE;

    if (prev_state == NVIDIA_3D0_SELECT) {
        /* Write MMIO index. */
        dev->quirks.nvidia3d0.index = val;
        dev->quirks.nvidia3d0.state = NVIDIA_3D0_WINDOW;
        vfio_log_op("VFIO %s: NVIDIA 3D0: Write index %04X\n", dev->name, val);
    } else if (prev_state == NVIDIA_3D0_WRITE) {
        /* Write configuration register if part of the main PCI configuration space. */
        if (((dev->quirks.nvidia3d0.index & 0xffffff00) == 0x00001800) || ((dev->quirks.nvidia3d0.index & 0xffffff00) == 0x00088000)) {
            vfio_log_op("VFIO %s: NVIDIA 3D0: Write %04X to index %08X\n", dev->name,
                        val, dev->quirks.nvidia3d0.index);
            vfio_config_writew(0, dev->quirks.nvidia3d0.index, val, 2, dev);
            return;
        }
    }

    /* Cascade to the main handler. */
    vfio_io_writew_fd(addr, val, &dev->vga_io_hi);
}

static void
vfio_quirk_nvidia3d0_data_writel(uint16_t addr, uint32_t val, void *priv)
{
    vfio_device_t *dev = (vfio_device_t *) priv;

    uint8_t prev_state          = dev->quirks.nvidia3d0.state;
    dev->quirks.nvidia3d0.state = NVIDIA_3D0_NONE;

    if (prev_state == NVIDIA_3D0_SELECT) {
        /* Write MMIO index. */
        dev->quirks.nvidia3d0.index = val;
        dev->quirks.nvidia3d0.state = NVIDIA_3D0_WINDOW;
        vfio_log_op("VFIO %s: NVIDIA 3D0: Write index %08X\n", dev->name, val);
    } else if (prev_state == NVIDIA_3D0_WRITE) {
        /* Write configuration register if part of the main PCI configuration space. */
        if (((dev->quirks.nvidia3d0.index & 0xffffff00) == 0x00001800) || ((dev->quirks.nvidia3d0.index & 0xffffff00) == 0x00088000)) {
            /* Write configuration register. */
            vfio_log_op("VFIO %s: NVIDIA 3D0: Write %08X to index %08X\n", dev->name,
                        val, dev->quirks.nvidia3d0.index);
            vfio_config_writel(0, dev->quirks.nvidia3d0.index, val, 4, dev);
            return;
        }
    }

    /* Cascade to the main handler. */
    vfio_io_writel_fd(addr, val, &dev->vga_io_hi);
}

static void
vfio_quirk_remap(vfio_device_t *dev, vfio_region_t *bar, uint8_t enable)
{
    /* Read vendor ID. */
    uint16_t vendor;
    if (pread(dev->config.fd, &vendor, sizeof(vendor), dev->config.offset) != sizeof(vendor))
        vendor = 0x0000;

    int i, j;
    switch (vendor) {
        case 0x1002: /* ATI */
            i = (vfio_bar_gettype(dev, &dev->bars[1]) == 0x01) && (dev->bars[1].size >= 256);
            j = (vfio_bar_gettype(dev, &dev->bars[4]) == 0x01) && (dev->bars[4].size >= 256);

            /* ATI/AMD cards report the I/O BAR's high byte on port 3C3, and according
               to the Red Hat slide deck, this is used for VBIOS bootstrapping purposes.
               This I/O BAR can be either 1 or 4, so we probe which one it is. If unsure
               (shouldn't really happen), pick 1 which is mostly used by older cards. */
            if ((bar == &dev->vga_io_hi) && (i || j)) {
                dev->quirks.ati3c3.bar = (j && !i) ? &dev->bars[4] : &dev->bars[1];
                vfio_log("VFIO %s: %sapping ATI 3C3 quirk (BAR %d)\n", dev->name,
                         enable ? "M" : "Unm", dev->quirks.ati3c3.bar->bar_id);
                vfio_quirk_capture_io(dev, bar, 0x3c3, 1, enable,
                                      vfio_quirk_ati3c3_readb, NULL, NULL,
                                      NULL, NULL, NULL);
            }

            /* BAR 2 configuration space mirror, and BAR 1/4 configuration space window. */
            if (j && !i) {
                /* QEMU only enables the mirror here if BAR 2 is 64-bit capable. */
                if ((bar->bar_id == 2) && ((vfio_config_readb(0, 0x18, 1, dev) & 0x07) == 0x04))
                    vfio_quirk_configmirror(dev, bar, 0x4000, 0, enable);
                else if (bar->bar_id == 4)
                    vfio_quirk_configwindow(dev, bar, 0x00, 4, 0x04, 4, 0x4000, 0x4000, enable);
            } else {
                if (bar->bar_id == 2)
                    vfio_quirk_configmirror(dev, bar, 0xf00, 0, enable);
                else if (bar->bar_id == 1)
                    vfio_quirk_configwindow(dev, bar, 0x00, 4, 0x04, 4, 0xf00, 0xf00, enable);
            }
            break;

        case 0x1023: /* Trident */
            /* Mirror TGUI acceleration port range to memory-mapped space, since the PCI bridge
               VGA decode policy doesn't allow it to be forwarded directly to the real card. */
            if ((bar->bar_id == 1) && (vfio_bar_gettype(dev, bar) == 0x00) && (bar->size >= 65536)) {
                /* Port range from vid_tgui9440.c */
                vfio_quirk_iomirror(dev, bar, 0, 0x2100, 256, enable);
            }
            break;

        case 0x10de: /* NVIDIA */
            /* BAR 0 configuration space mirrors. */
            if ((bar->bar_id == 0) && (vfio_bar_gettype(dev, bar) == 0x00)) {
                vfio_quirk_configmirror(dev, bar, 0x1800, 0, enable);
                vfio_quirk_configmirror(dev, bar, 0x88000, 1, enable);
            }

            /* BAR 5 configuration space window. */
            if ((bar->bar_id == 5) && (vfio_bar_gettype(dev, bar) == 0x01)) {
                vfio_log("VFIO %s: %sapping NVIDIA BAR 5 quirk\n", dev->name, enable ? "M" : "Unm");
                vfio_quirk_capture_io(dev, bar, bar->emulated_offset, 8, enable,
                                      vfio_io_readb_fd,
                                      vfio_io_readw_fd,
                                      vfio_io_readl_fd,
                                      vfio_quirk_nvidiabar5_writeb,
                                      vfio_quirk_nvidiabar5_writew,
                                      vfio_quirk_nvidiabar5_writel);

                /* Update window to account for changes in BAR enable status. */
                dev->quirks.nvidiabar5.bar_enable = enable;
                vfio_quirk_nvidiabar5(dev);
            }

            /* Port 3D0 configuration space window. */
            if ((bar == &dev->vga_io_hi) && dev->bars[1].size) {
                vfio_log("VFIO %s: %sapping NVIDIA 3D0 quirk\n", dev->name, enable ? "M" : "Unm");
                vfio_quirk_capture_io(dev, bar, 0x3d0, 1, enable,
                                      vfio_quirk_nvidia3d0_data_readb,
                                      vfio_quirk_nvidia3d0_data_readw,
                                      vfio_quirk_nvidia3d0_data_readl,
                                      vfio_quirk_nvidia3d0_data_writeb,
                                      vfio_quirk_nvidia3d0_data_writew,
                                      vfio_quirk_nvidia3d0_data_writel);
                vfio_quirk_capture_io(dev, bar, 0x3d4, 1, enable,
                                      vfio_quirk_nvidia3d0_state_readb,
                                      vfio_quirk_nvidia3d0_state_readw,
                                      vfio_quirk_nvidia3d0_state_readl,
                                      vfio_quirk_nvidia3d0_state_writeb,
                                      vfio_quirk_nvidia3d0_state_writew,
                                      vfio_quirk_nvidia3d0_state_writel);
            }
            break;

        case 0x5333: /* S3 */
            /* Mirror enhanced command port ranges to memory-mapped space, since the PCI bridge
               VGA decode policy doesn't allow those to be forwarded directly to the real card. */
            if (vfio_bar_gettype(dev, &dev->bars[0]) != 0x00)
                break;
            if ((dev->bars[0].size == 33554432) && (dev->bar_count == 1)) {
                /* Older chips can only remap to VGA A0000. We can tell
                   those through BAR 0 being 32M and the only BAR. */
                if (bar == &dev->vga_mem) {
                    i = 0;

                    /* Main port list from vid_s3.c */
                    vfio_quirk_iomirror(dev, bar, i, 0x42e8, 2, enable);
                    vfio_quirk_iomirror(dev, bar, i, 0x46e8, 2, enable);
                    vfio_quirk_iomirror(dev, bar, i, 0x4ae8, 2, enable);

s3_old_mmio:
                    vfio_quirk_iomirror(dev, bar, i, 0x82e8, 4, enable);
                    vfio_quirk_iomirror(dev, bar, i, 0x86e8, 4, enable);
                    vfio_quirk_iomirror(dev, bar, i, 0x8ae8, 4, enable);
                    vfio_quirk_iomirror(dev, bar, i, 0x8ee8, 4, enable);
                    vfio_quirk_iomirror(dev, bar, i, 0x92e8, 4, enable);
                    vfio_quirk_iomirror(dev, bar, i, 0x96e8, 4, enable);

                    vfio_quirk_iomirror(dev, bar, i, 0x9ae8, 4, enable);

                    vfio_quirk_iomirror(dev, bar, i, 0x9ee8, 2, enable);
                    vfio_quirk_iomirror(dev, bar, i, 0xa2e8, 4, enable);
                    vfio_quirk_iomirror(dev, bar, i, 0xa6e8, 4, enable);
                    vfio_quirk_iomirror(dev, bar, i, 0xaae8, 4, enable);
                    vfio_quirk_iomirror(dev, bar, i, 0xaee8, 4, enable);

                    vfio_quirk_iomirror(dev, bar, i, 0xb2e8, 4, enable);

                    vfio_quirk_iomirror(dev, bar, i, 0xb6e8, 2, enable);
                    vfio_quirk_iomirror(dev, bar, i, 0xbae8, 2, enable);
                    vfio_quirk_iomirror(dev, bar, i, 0xbee8, 2, enable);
                    vfio_quirk_iomirror(dev, bar, i, 0xe2e8, 2, enable);

                    vfio_quirk_iomirror(dev, bar, i, 0xd2e8, 2, enable);
                    vfio_quirk_iomirror(dev, bar, i, 0xe6e8, 4, enable);
                    vfio_quirk_iomirror(dev, bar, i, 0xeae8, 4, enable);
                    vfio_quirk_iomirror(dev, bar, i, 0xeee8, 4, enable);
                }
            } else if ((dev->bars[0].size == 67108864) && (dev->bar_count == 1)) {
                /* Trio64V+ and ViRGE chips can remap to BAR 0 + 16M. We can tell those through
                   BAR 0 being 64M = ((16M linear + 16M MMIO) * both endians) and the only BAR. */
                if (bar->bar_id == 0) {
                    i = 0x1000000; /* 16M MMIO offset */

s3_new_mmio: /* There's a configuration space mirror in here as well. */
                    vfio_quirk_configmirror(dev, bar, i + 0x8000, 0, enable);

                    /* Subsystem Control/Status and Advanced Function Control. */
                    vfio_quirk_iomirror(dev, bar, i + 0x8504 - 0x42e8, 0x42e8, 2, enable);
                    vfio_quirk_iomirror(dev, bar, i + 0x850c - 0x4ae8, 0x4ae8, 2, enable);

                    /* The rest maps exactly as older chips. */
                    goto s3_old_mmio;
                }
            } else if ((dev->bars[0].size >= 524288) && (vfio_bar_gettype(dev, &dev->bars[1]) == 0x00)) {
                /* Savage chips break the linear framebuffer out to
                   BAR 1+, eliminating the 16M MMIO offset from BAR 0. */
                if (bar->bar_id == 0) {
                    i = 0;
                    goto s3_new_mmio;
                }
            }
            break;
    }
}

static uint8_t
vfio_bar_gettype(vfio_device_t *dev, vfio_region_t *bar)
{
    /* Read and store BAR type from device if unknown. */
    if (bar->type == 0xff) {
        if (pread(dev->config.fd, &bar->type, sizeof(bar->type),
                  dev->config.offset + 0x10 + (bar->bar_id << 2))
            == sizeof(bar->type))
            bar->type &= 0x01;
        else
            bar->type = 0xff;
    }

    /* Return stored BAR type. */
    return bar->type;
}

static void
vfio_bar_remap(vfio_device_t *dev, vfio_region_t *bar, uint32_t new_offset)
{
    vfio_log("VFIO %s: bar_remap(%s, %08X)\n", dev->name, bar->name, new_offset);

    /* Act according to the BAR type. */
    uint8_t bar_type = vfio_bar_gettype(dev, bar);
    if (bar_type == 0x00) { /* Memory BAR */
        if (bar->emulated_offset) {
            vfio_log("VFIO %s: Unmapping %s memory @ %08X-%08X\n", dev->name,
                     bar->name, bar->emulated_offset, bar->emulated_offset + bar->size - 1);

            /* Unmap any quirks. */
            vfio_quirk_remap(dev, bar, 0);

            /* Disable memory mapping. */
            mem_mapping_disable(&bar->mem_mapping);

            /* Disable MSI-X table and PBA mappings if applicable to this BAR. */
            if (dev->irq.msix.table_bar == bar->bar_id)
                mem_mapping_disable(&dev->irq.msix.table_mapping);
            if (dev->irq.msix.pba_bar == bar->bar_id)
                mem_mapping_disable(&dev->irq.msix.pba_mapping);
        }

        bar->mmap_precalc = bar->mmap_base - new_offset;
        /* Expansion ROM requires both ROM enable and memory enable. */
        if (((bar->bar_id != 0xff) || dev->rom_enabled) && dev->mem_enabled && new_offset) {
            vfio_log("VFIO %s: Mapping %s memory @ %08X-%08X\n", dev->name,
                     bar->name, new_offset, new_offset + bar->size - 1);

            /* Enable memory mapping. */
            if (bar->mmap_base) /* mmap available */
                mem_mapping_set_p(&bar->mem_mapping, bar->mmap_precalc);
            mem_mapping_set_addr(&bar->mem_mapping, new_offset, bar->size);

            /* Map any quirks. */
            vfio_quirk_remap(dev, bar, 1);

            /* Enable MSI-X table and PBA mappings if applicable to this BAR. */
            if (dev->irq.msix.table_bar == bar->bar_id) {
                dev->irq.msix.table_offset_precalc = new_offset + dev->irq.msix.table_offset;
                mem_mapping_set_addr(&dev->irq.msix.table_mapping,
                                     dev->irq.msix.table_offset_precalc,
                                     dev->irq.msix.table_size);
            }
            if (dev->irq.msix.pba_bar == bar->bar_id) {
                dev->irq.msix.pba_offset_precalc = new_offset + dev->irq.msix.pba_offset;
                mem_mapping_set_addr(&dev->irq.msix.pba_mapping,
                                     dev->irq.msix.pba_offset_precalc,
                                     dev->irq.msix.pba_size);
            }
        }
    } else if (bar_type == 0x01) { /* I/O BAR */
        if (bar->emulated_offset) {
            vfio_log("VFIO %s: Unmapping %s I/O @ %04X-%04X\n", dev->name,
                     bar->name, bar->emulated_offset, bar->emulated_offset + bar->size - 1);

            /* Unmap any quirks. */
            vfio_quirk_remap(dev, bar, 0);

            /* Disable I/O mapping. */
            if (bar->mmap_base) /* mmap available */
                io_removehandler(bar->emulated_offset, bar->size,
                                 bar->read ? vfio_io_readb_mm : NULL,
                                 bar->read ? vfio_io_readw_mm : NULL,
                                 bar->read ? vfio_io_readl_mm : NULL,
                                 bar->write ? vfio_io_writeb_mm : NULL,
                                 bar->write ? vfio_io_writew_mm : NULL,
                                 bar->write ? vfio_io_writel_mm : NULL,
                                 bar->mmap_precalc);
            else /* mmap not available */
                io_removehandler(bar->emulated_offset, bar->size,
                                 bar->read ? vfio_io_readb_fd : NULL,
                                 bar->read ? vfio_io_readw_fd : NULL,
                                 bar->read ? vfio_io_readl_fd : NULL,
                                 bar->write ? vfio_io_writeb_fd : NULL,
                                 bar->write ? vfio_io_writew_fd : NULL,
                                 bar->write ? vfio_io_writel_fd : NULL,
                                 bar);
        }

        bar->mmap_precalc = bar->mmap_base - new_offset;
        if (dev->io_enabled && new_offset) {
            vfio_log("VFIO %s: Mapping %s I/O @ %04X-%04X\n", dev->name,
                     bar->name, new_offset, new_offset + bar->size - 1);

            /* Enable I/O mapping. */
            if (bar->mmap_base) /* mmap available */
                io_sethandler(new_offset, bar->size,
                              bar->read ? vfio_io_readb_mm : NULL,
                              bar->read ? vfio_io_readw_mm : NULL,
                              bar->read ? vfio_io_readl_mm : NULL,
                              bar->write ? vfio_io_writeb_mm : NULL,
                              bar->write ? vfio_io_writew_mm : NULL,
                              bar->write ? vfio_io_writel_mm : NULL,
                              bar->mmap_precalc);
            else /* mmap not available */
                io_sethandler(new_offset, bar->size,
                              bar->read ? vfio_io_readb_fd : NULL,
                              bar->read ? vfio_io_readw_fd : NULL,
                              bar->read ? vfio_io_readl_fd : NULL,
                              bar->write ? vfio_io_writeb_fd : NULL,
                              bar->write ? vfio_io_writew_fd : NULL,
                              bar->write ? vfio_io_writel_fd : NULL,
                              bar);

            /* Map any quirks. */
            vfio_quirk_remap(dev, bar, 1);
        }
    }

    /* Set new emulated and precalculated offsets.
       The precalculated offsets speed up read/write operations. */
    bar->emulated_offset = new_offset;
    bar->precalc_offset  = bar->offset - new_offset;
}

static uint32_t
ceilpow2(uint32_t size)
{
    uint32_t pow_size = 1 << log2i(size);
    if (pow_size < size)
        return pow_size << 1;
    return pow_size;
}

static uint8_t
vfio_config_readb(int func, int addr, UNUSED(int len), void *priv)
{
    vfio_device_t *dev = (vfio_device_t *) priv;
    if (func)
        return 0xff;

    intx_high = 0;

    /* Read register from device. */
    addr &= 0xff;
    uint8_t ret;
    if (pread(dev->config.fd, &ret, 1, dev->config.offset + addr) != 1) {
        vfio_log("VFIO %s: config_readb(%d, %02X) failed\n", dev->name,
                 func, addr);
        return 0xff;
    }

    /* Change value accordingly. */
    uint8_t bar_id, offset, new;
    switch (addr) {
        case 0x10 ... 0x27: /* BARs */
            /* Stop if this BAR is absent. */
            bar_id = (addr - 0x10) >> 2;
            if (!dev->bars[bar_id].read && !dev->bars[bar_id].write) {
                ret = 0x00;
                break;
            }

            /* Mask off and insert static bits. */
            offset = (addr & 3) << 3;
            new    = dev->bars[bar_id].emulated_offset >> offset;
            if (!offset) {
                switch (vfio_bar_gettype(dev, &dev->bars[bar_id])) {
                    case 0x00: /* Memory BAR */
                        new = (new & ~0x07) | (ret & 0x07);
                        break;

                    case 0x01: /* I/O BAR */
                        new = (new & ~0x03) | (ret & 0x03);
                        break;
                }
            }
            ret = new;
            break;

        case 0x30 ... 0x33: /* Expansion ROM */
            /* Stop if the ROM is absent. */
            if (!dev->rom.read) {
                ret = 0x00;
                break;
            }

            /* Mask off and insert ROM enable bit. */
            offset = (addr & 3) << 3;
            ret    = dev->rom.emulated_offset >> offset;
            if (!offset)
                ret = (ret & ~0x01) | dev->rom_enabled;
            break;

        default:                                          /* other (capabilities) */
            if (dev->msi_cap && (addr >= dev->msi_cap)) { /* MSI */
                /* Adjust register offset to account for different structure levels. */
                offset = addr - dev->msi_cap;
                if (!(dev->irq.msi.ctl & 0x0080) && (offset >= 0x08))
                    offset += 4;
                switch (offset) {
                    case 0x02 ... 0x03: /* Message Control */
                        offset = (offset - 0x02) << 3;
                        ret    = dev->irq.msi.ctl >> offset;
                        goto end;

                    case 0x04 ... 0x07: /* Message Address */
                        offset = (offset - 0x04) << 3;
                        ret    = dev->irq.msi.address >> offset;
                        goto end;

                    case 0x08 ... 0x0b: /* Message Upper Address */
                        offset = (offset - 0x08) << 3;
                        ret    = dev->irq.msi.address_upper >> offset;
                        goto end;

                    case 0x0c ... 0x0d: /* Message Data */
                        offset = (offset - 0x0c) << 3;
                        ret    = dev->irq.msi.data >> offset;
                        goto end;

                    case 0x10 ... 0x13: /* Mask Bits */
                        if (dev->irq.msi.ctl & 0x0100) {
                            offset = (offset - 0x10) << 3;
                            ret    = dev->irq.msi.mask >> offset;
                            goto end;
                        }
                        break;

                    case 0x14 ... 0x17: /* Pending Bits */
                        if (dev->irq.msi.ctl & 0x0100) {
                            offset = (offset - 0x14) << 3;
                            ret    = dev->irq.msi.pending >> offset;
                            goto end;
                        }
                        break;
                }
            }
            if (dev->msix_cap && (addr >= dev->msix_cap)) { /* MSI-X */
                offset = addr - dev->msix_cap;
                switch (offset) {
                    case 0x02 ... 0x03: /* Message Control */
                        offset = (offset - 0x02) << 3;
                        ret    = dev->irq.msix.ctl >> offset;
                        goto end;
                }
            }
end:
            break;
    }

    vfio_log("VFIO %s: config_readb(%02X) = %02X\n", dev->name,
             addr, ret);

    return ret;
}

static uint16_t
vfio_config_readw(int func, int addr, UNUSED(int len), void *priv)
{
    return vfio_config_readb(func, addr, 2, priv) | (vfio_config_readb(func, addr + 1, 2, priv) << 8);
}

static uint32_t
vfio_config_readl(int func, int addr, UNUSED(int len), void *priv)
{
    return vfio_config_readb(func, addr, 4, priv) | (vfio_config_readb(func, addr + 1, 4, priv) << 8) | (vfio_config_readb(func, addr + 2, 4, priv) << 16) | (vfio_config_readb(func, addr + 3, 4, priv) << 24);
}

static void
vfio_config_writeb(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    vfio_device_t *dev = (vfio_device_t *) priv;
    if (func)
        return;

    addr &= 0xff;
    vfio_log("VFIO %s: config_writeb(%02X, %02X)\n", dev->name, addr, val);

    intx_high = 0;

    /* VFIO should block anything we shouldn't write to, such as BARs. */
    (void) !pwrite(dev->config.fd, &val, 1, dev->config.offset + addr);

    /* Act on some written values. */
    uint8_t  new_mem_enabled;
    uint8_t  new_io_enabled;
    uint8_t  bar_id;
    uint8_t  offset;
    uint32_t new_value;
    uint64_t val64;

    switch (addr) {
        case 0x04: /* Command */
            /* Determine new memory and I/O enable states. */
            new_mem_enabled = !!(val & PCI_COMMAND_MEM);
            new_io_enabled  = !!(val & PCI_COMMAND_IO);

            vfio_log("VFIO %s: Command Memory[%d] I/O[%d]\n", dev->name,
                     new_mem_enabled, new_io_enabled);

            /* Remap regions only if their respective enable bits have changed. */
            if (dev->mem_enabled ^ new_mem_enabled) {
                /* Set new memory enable state. */
                dev->mem_enabled = new_mem_enabled;

                /* Remap memory BARs. */
                for (uint8_t i = 0; i < 6; i++) {
                    if (vfio_bar_gettype(dev, &dev->bars[i]) == 0x00)
                        vfio_bar_remap(dev, &dev->bars[i], dev->bars[i].emulated_offset);
                }

                /* Remap ROM if present. */
                if (dev->rom.read)
                    vfio_bar_remap(dev, &dev->rom, dev->rom.emulated_offset);

                /* Remap VGA framebuffer region if present. */
                if (dev->vga_mem.bar_id)
                    vfio_bar_remap(dev, &dev->vga_mem, 0xa0000);
            }
            if (dev->io_enabled ^ new_io_enabled) {
                /* Set new I/O enable state. */
                dev->io_enabled = new_io_enabled;

                /* Remap I/O BARs. */
                for (uint8_t i = 0; i < 6; i++) {
                    if (vfio_bar_gettype(dev, &dev->bars[i]) == 0x01)
                        vfio_bar_remap(dev, &dev->bars[i], dev->bars[i].emulated_offset);
                }

                /* Remap VGA I/O regions if present. */
                if (dev->vga_io_lo.bar_id) {
                    vfio_bar_remap(dev, &dev->vga_io_lo, 0x3b0);
                    vfio_bar_remap(dev, &dev->vga_io_hi, 0x3c0);
                }
            }
            break;

        case 0x10 ... 0x27: /* BARs */
            /* Stop if this BAR is absent. */
            bar_id = (addr - 0x10) >> 2;
            if (!dev->bars[bar_id].read && !dev->bars[bar_id].write)
                break;

            /* Mask off static bits. */
            offset = (addr & 3) << 3;
            if (!offset) {
                switch (vfio_bar_gettype(dev, &dev->bars[bar_id])) {
                    case 0x00: /* Memory BAR */
                        val &= ~0x07;
                        break;

                    case 0x01: /* I/O BAR */
                        val &= ~0x03;
                        break;
                }
            }

            /* Remap BAR. */
            new_value = dev->bars[bar_id].emulated_offset & ~(0x000000ff << offset);
            new_value |= val << offset;
            new_value &= ~(ceilpow2(dev->bars[bar_id].size) - 1);
            vfio_bar_remap(dev, &dev->bars[bar_id], new_value);
            break;

        case 0x30 ... 0x33: /* Expansion ROM */
            /* Stop if the ROM is absent. */
            if (!dev->rom.read)
                break;

            /* Set ROM enable bit. */
            offset = (addr & 3) << 3;
            if (!offset) {
                dev->rom_enabled = val & 0x01;
                val &= 0xfe;
            }

            /* Remap ROM. */
            new_value = (dev->rom.emulated_offset & ~(0x000000ff << offset));
            new_value |= val << offset;
            new_value &= ~(ceilpow2(dev->rom.size) - 1);
            vfio_bar_remap(dev, &dev->rom, new_value);
            break;

        case 0x3d: /* Interrupt Pin */
            if (val != dev->irq.intx.pin)
                vfio_irq_intx_setpin(dev);
            break;

        default:                                          /* other (capabilities) */
            if (dev->msi_cap && (addr >= dev->msi_cap)) { /* MSI */
                /* Adjust register offset to account for different structure levels. */
                offset = addr - dev->msi_cap;
                if (!(dev->irq.msi.ctl & 0x0080) && (offset >= 0x08))
                    offset += 4;
                switch (offset) {
                    case 0x00 ... 0x01: /* Capability */
                        goto end;

                    case 0x02 ... 0x03: /* Message Control */
                        offset    = (offset - 0x02) << 3;
                        new_value = dev->irq.msi.ctl & ~(0x00ff << offset);
                        new_value |= val << offset;

                        /* Enable or disable MSI if requested and not conflicting with MSI-X. */
                        if (dev->irq.type != VFIO_PCI_MSIX_IRQ_INDEX) {
                            if (!(dev->irq.msi.ctl & 0x0001) && (new_value & 0x0001))
                                vfio_irq_enable(dev, VFIO_PCI_MSI_IRQ_INDEX);
                            else if ((dev->irq.msi.ctl & 0x0001) && !(new_value & 0x0001))
                                vfio_irq_msi_disable(dev);
                        }

                        /* Update control register. */
                        dev->irq.msi.ctl = (new_value & 0x0071) | (dev->irq.msi.ctl & 0xff8e);

                        /* Update enabled vector count and mask. */
                        dev->irq.msi.vector_enable_count = MIN(1 << ((dev->irq.msi.ctl >> 1) & 3), dev->irq.msi.vector_count);
                        dev->irq.msi.vector_enable_mask  = dev->irq.msi.vector_enable_count - 1;
                        goto end;

                    case 0x04 ... 0x07: /* Message Address */
                        offset    = (offset - 0x04) << 3;
                        new_value = dev->irq.msi.address & ~(0x000000ff << offset);
                        new_value |= val << offset;
                        dev->irq.msi.address = new_value & 0xfffffffc;
                        goto end;

                    case 0x08 ... 0x0b: /* Message Upper Address */
                        offset    = (offset - 0x08) << 3;
                        new_value = dev->irq.msi.address_upper & ~(0x000000ff << offset);
                        new_value |= val << offset;
                        dev->irq.msi.address_upper = new_value;
                        goto end;

                    case 0x0c ... 0x0d: /* Message Data */
                        offset    = (offset - 0x0c) << 3;
                        new_value = dev->irq.msi.data & ~(0x00ff << offset);
                        new_value |= val << offset;
                        dev->irq.msi.data = new_value;
                        goto end;

                    case 0x0e ... 0x0f: /* Reserved */
                    case 0x14 ... 0x17: /* Pending Bits */
                        if (dev->irq.msi.ctl & 0x0100)
                            goto end;
                        break;

                    case 0x10 ... 0x13: /* Mask Bits */
                        if (dev->irq.msi.ctl & 0x0100) {
                            offset    = (offset - 0x10) << 3;
                            new_value = dev->irq.msi.mask & ~(0x000000ff << offset);
                            new_value |= val << offset;
                            dev->irq.msi.mask = new_value;

                            /* Service any unmasked pending interrupts if MSI is enabled. */
                            if (dev->irq.msi.ctl & 0x0001) {
                                new_value = ~new_value;
                                val64     = 1;
                                for (uint8_t i = 0; i < dev->irq.msi.vector_enable_count; i++) {
                                    if (dev->irq.msi.pending & ((1 << i) & new_value))
                                        (void) !write(dev->irq.vectors[i].fd, &val64, sizeof(val64));
                                }
                                dev->irq.msi.pending &= new_value;
                            }

                            goto end;
                        }
                        break;
                }
            }
            if (dev->msix_cap && (addr >= dev->msix_cap)) { /* MSI-X */
                offset = addr - dev->msix_cap;
                switch (offset) {
                    case 0x00 ... 0x01: /* Capability */
                    case 0x04 ... 0x0b: /* Table/PBA Offset */
                        goto end;

                    case 0x02 ... 0x03: /* Message Control */
                        offset    = (offset - 0x02) << 3;
                        new_value = dev->irq.msix.ctl & ~(0x00ff << offset);
                        new_value |= val << offset;

                        /* Enable or disable MSI-X if requested. */
                        if (!(dev->irq.msix.ctl & 0x8000) && (new_value & 0x8000))
                            vfio_irq_enable(dev, VFIO_PCI_MSIX_IRQ_INDEX);
                        else if ((dev->irq.msix.ctl & 0x8000) && !(new_value & 0x8000))
                            vfio_irq_msix_disable(dev);

                        /* Update control register. */
                        dev->irq.msix.ctl = (new_value & 0xc000) | (dev->irq.msix.ctl & 0x3fff);

                        /* Service any unmasked pending interrupts if MSI-X
                           is enabled and the global mask bit was cleared. */
                        if ((dev->irq.msix.ctl & 0xc000) == 0x8000) {
                            for (uint16_t i = 0x000c; i < dev->irq.msix.table_size; i += 0x0010)
                                vfio_irq_msix_updatemask(dev, i);
                        }
                        goto end;
                }
            }
end:
            break;
    }
}

static void
vfio_config_writew(int func, int addr, UNUSED(int len), uint16_t val, void *priv)
{
    vfio_config_writeb(func, addr, 2, val, priv);
    vfio_config_writeb(func, addr | 1, 2, val >> 8, priv);
}

static void
vfio_config_writel(int func, int addr, UNUSED(int len), uint32_t val, void *priv)
{
    vfio_config_writeb(func, addr, 4, val, priv);
    vfio_config_writeb(func, addr | 1, 4, val >> 8, priv);
    vfio_config_writeb(func, addr | 2, 4, val >> 16, priv);
    vfio_config_writeb(func, addr | 3, 4, val >> 24, priv);
}

static void
vfio_irq_thread(void *priv)
{
    int                 nfds, i;
    uint64_t            buf;
    struct epoll_event  events[16];
    struct vfio_irq_set irq_set = {
        .argsz = sizeof(irq_set),
        .index = VFIO_PCI_INTX_IRQ_INDEX,
        .start = 0,
        .count = 1
    };
    vfio_device_t *dev;
    vfio_irq_t    *irq;

    vfio_log("VFIO: IRQ thread started\n");

    while (epoll_fd >= 0) {
        /* Wait for an interrupt to come in. */
        nfds = epoll_wait(epoll_fd, events, sizeof(events) / sizeof(events[0]), -1);
        if (nfds < 0) {
            vfio_log("VFIO %s: epoll_wait failed (%d)\n", errno);
            break;
        }

        /* Process all interrupts which came in. */
        for (i = 0; i < nfds; i++) {
            /* Only handle read events. */
            if (!(events[i].events & EPOLLIN))
                continue;

            /* Get the IRQ and device structures for this interrupt. */
            irq = (vfio_irq_t *) events[i].data.ptr;
            if (!irq) {
                /* Do nothing if this is the wake eventfd, which has no data. */
                (void) !read(irq_thread_wake_fd, &buf, sizeof(buf));
                continue;
            }
            dev = irq->dev;

            /* Reset eventfd counter. */
            (void) !read(irq->fd, &buf, sizeof(buf));

            /* Don't hang waiting for the timer if we're closing. */
            if (closing)
                continue;

            /* Log VFIO IRQ type and vector. */
            vfio_log_op("VFIO %s: %s IRQ on vector %d\n", dev->name,
                        ((irq->type == VFIO_PCI_INTX_IRQ_INDEX) ? "INTx" : (((irq->type == VFIO_PCI_MSI_IRQ_INDEX) ? "MSI" : ((irq->type == VFIO_PCI_MSIX_IRQ_INDEX) ? "MSI-X" : NULL)))),
                        irq->vector);

            /* Perform pre-checks for specific IRQ types. */
            switch (irq->type) {
                case VFIO_PCI_INTX_IRQ_INDEX:
                    /* Mask host IRQ. */
                    irq_set.flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_MASK;
                    ioctl(dev->fd, VFIO_DEVICE_SET_IRQS, &irq_set);
                    break;

                case VFIO_PCI_MSI_IRQ_INDEX:
                    /* Ignore MSI if this vector is not enabled. */
                    if (irq->vector >= dev->irq.msi.vector_enable_count) {
                        vfio_log_op("VFIO %s: MSI vector not enabled (%d >= %d)\n", dev->name,
                                    irq->vector, dev->irq.msi.vector_enable_count);
                        continue;
                    }

                    /* Ignore MSI if the upper 32 bits of a 64-bit address are non-zero. */
                    if (dev->irq.msi.address_upper) {
                        vfio_log_op("VFIO %s: MSI 64-bit address %08X%08X\n", dev->name,
                                    dev->irq.msi.address_upper, dev->irq.msi.address);
                        continue;
                    }

                    /* Mark MSI as pending if this vector is masked through per-vector masking. */
                    if (dev->irq.msi.mask & (1 << irq->vector)) {
                        vfio_log_op("VFIO %s: MSI masked\n", dev->name);
                        dev->irq.msi.pending |= 1 << irq->vector;
                        continue;
                    }
                    break;

                case VFIO_PCI_MSIX_IRQ_INDEX:
                    /* Ignore MSI-X if the upper 32 bits of a 64-bit address are non-zero. */
                    if (*((uint32_t *) &dev->irq.msix.table[irq->msix_offset | 0x4])) {
                        vfio_log_op("VFIO %s: MSI-X 64-bit address %016X\n", dev->name,
                                    *((uint64_t *) &dev->irq.msix.table[irq->msix_offset]));
                        continue;
                    }

                    /* Mark MSI-X as pending if this vector or all vectors are masked. */
                    if ((dev->irq.msix.ctl & 0x4000) || (dev->irq.msix.table[irq->msix_offset | 0xc] & 0x01)) {
                        vfio_log_op("VFIO %s: MSI-X masked\n", dev->name);
                        dev->irq.msix.pba[irq->vector >> 3] |= 1 << (irq->vector & 0x07);
                        continue;
                    }
                    break;
            }

            /* Tell the timer to service this interrupt. */
            current_irq = irq;

            /* Wait for the timer to do its job. */
            thread_wait_event(irq_event, -1);
            thread_reset_event(irq_event);
            vfio_log_op("VFIO %s: IRQ serviced\n", dev->name);

            /* Unmask host IRQ if this is INTx. */
            if (irq->type == VFIO_PCI_INTX_IRQ_INDEX) {
                irq_set.flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_UNMASK;
                ioctl(dev->fd, VFIO_DEVICE_SET_IRQS, &irq_set);
            }
        }

        /* Pause if we were asked to. */
        thread_wait_event(irq_thread_resume, -1);
    }

    /* We're done here. */
    vfio_log("VFIO: IRQ thread finished\n");
}

static void
vfio_irq_timer(void *priv)
{
    /* Schedule next run. */
    timer_on_auto(&irq_timer, 100.0);

    /* Stop if we're not servicing an IRQ at the moment. */
    if (!current_irq)
        return;
    vfio_device_t *dev = current_irq->dev;

    /* Act according to the IRQ type. */
    uint16_t val;
    switch (current_irq->type) {
        case VFIO_PCI_INTX_IRQ_INDEX:
            if (!dev->irq.intx.raised) { /* rising edge */
                vfio_log_op("VFIO %s: Raising IRQ on pin INT%c\n", dev->name,
                            '@' + dev->irq.intx.pin);

                /* Raise IRQ. */
                pci_set_irq(dev->slot, dev->irq.intx.pin, &dev->irq.intx.state);

                /* Mark the IRQ as active, so that a BAR read/write can lower it. */
                dev->irq.intx.raised = intx_high = 1;
            } else if (!intx_high) { /* falling edge */
                vfio_log_op("VFIO %s: Lowering IRQ on pin INT%c\n", dev->name,
                            '@' + dev->irq.intx.pin);

                /* Lower IRQ. */
                pci_clear_irq(dev->slot, dev->irq.intx.pin, &dev->irq.intx.state);

                /* Mark the IRQ as no longer high. */
                dev->irq.intx.raised = intx_high = 0;

                /* Allow the IRQ thread to be unblocked. */
                break;
            }

            /* Don't unblock the IRQ thread unless otherwise stated. */
            return;

        case VFIO_PCI_MSI_IRQ_INDEX:
            /* Insert the vector number into the value's lower bits. */
            val = (dev->irq.msi.data & ~dev->irq.msi.vector_enable_mask) | (current_irq->vector & dev->irq.msi.vector_enable_mask);

            /* Write value. */
            vfio_log_op("VFIO %s: Writing MSI value %04X to %04X\n", dev->name, val, dev->irq.msi.address);
            mem_writew_phys(dev->irq.msi.address, val);
            break;

        case VFIO_PCI_MSIX_IRQ_INDEX:
            /* Write value. */
            vfio_log_op("VFIO %s: Writing MSI-X value %08X to %08X\n", dev->name,
                        *((uint32_t *) &dev->irq.msix.table[current_irq->msix_offset | 0x8]),
                        *((uint32_t *) &dev->irq.msix.table[current_irq->msix_offset]));
            mem_writel_phys(*((uint32_t *) &dev->irq.msix.table[current_irq->msix_offset]),
                            *((uint32_t *) &dev->irq.msix.table[current_irq->msix_offset | 0x8]));
            break;
    }

    /* Unblock the IRQ thread. */
    current_irq = NULL;
    thread_set_event(irq_event);
}

static void
vfio_irq_disabletype(vfio_device_t *dev, int type)
{
    struct vfio_irq_set irq_set = {
        .argsz = sizeof(irq_set),
        .flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_TRIGGER,
        .index = type,
        .start = 0,
        .count = 0,
    };
    ioctl(dev->fd, VFIO_DEVICE_SET_IRQS, &irq_set);
}

static void
vfio_irq_intx_disable(vfio_device_t *dev)
{
    /* Disable INTx on VFIO. */
    vfio_irq_disabletype(dev, VFIO_PCI_INTX_IRQ_INDEX);

    /* Clear pending interrupts. */
    dev->irq.intx.raised = intx_high = 0;
    if (dev->irq.intx.pin)
        pci_clear_irq(dev->slot, dev->irq.intx.pin, &dev->irq.intx.state);

    /* Disable interrupts altogether. */
    dev->irq.type = VFIO_PCI_NUM_IRQS;
}

static void
vfio_irq_intx_setpin(vfio_device_t *dev)
{
    uint8_t val;
    if (pread(dev->config.fd, &val, sizeof(val), dev->config.offset + 0x3d) == sizeof(val))
        dev->irq.intx.pin = val;
    vfio_log("VFIO %s: IRQ pin is INT%c\n", dev->name, '@' + MIN(dev->irq.intx.pin, 'Z'));
}

static void
vfio_irq_msi_disable(vfio_device_t *dev)
{
    /* Clear pending interrupts. */
    dev->irq.msi.pending = 0;

    /* Disable MSI on VFIO. */
    vfio_irq_disabletype(dev, VFIO_PCI_MSI_IRQ_INDEX);

    /* Re-enable INTx interrupts. */
    vfio_irq_enable(dev, VFIO_PCI_INTX_IRQ_INDEX);
}

static void
vfio_irq_msix_disable(vfio_device_t *dev)
{
    /* Clear pending interrupts. */
    memset(dev->irq.msix.pba, 0, dev->irq.vector_count);

    /* Disable MSI-X on VFIO. */
    vfio_irq_disabletype(dev, VFIO_PCI_MSIX_IRQ_INDEX);

    /* Re-enable INTx interrupts. */
    vfio_irq_enable(dev, VFIO_PCI_INTX_IRQ_INDEX);
}

static void
vfio_irq_msix_updatemask(vfio_device_t *dev, uint16_t offset)
{
    /* Service any unmasked pending interrupts. */
    if (((dev->irq.msix.ctl & 0xc000) == 0x8000) && !(dev->irq.msix.table[offset] & 0x01) && (dev->irq.msix.pba[offset >> 7] & (1 << (offset & 0x07)))) {
        uint64_t val = 1;
        (void) !write(dev->irq.vectors[offset >> 4].fd, &val, sizeof(val));
        dev->irq.msix.pba[offset >> 7] &= ~(1 << (offset & 0x07));
    }
}

#define VFIO_RW_MSIX(length_char, val_type, val_slength)                                                                                           \
    static val_type                                                                                                                                \
        vfio_irq_msix_table_read##length_char(uint32_t addr, void *priv)                                                                           \
    {                                                                                                                                              \
        vfio_device_t *dev = (vfio_device_t *) priv;                                                                                               \
        val_type       ret = dev->irq.msix.table[addr - dev->irq.msix.table_offset_precalc];                                                       \
        vfio_log_op("[%08X:%04X] VFIO %s: msix_table_read" #length_char "(%08X) = %0" #val_slength "X\n", CS, cpu_state.pc, dev->name, addr, ret); \
        return ret;                                                                                                                                \
    }                                                                                                                                              \
                                                                                                                                                   \
    static void                                                                                                                                    \
        vfio_irq_msix_table_write##length_char(uint32_t addr, val_type val, void *priv)                                                            \
    {                                                                                                                                              \
        vfio_device_t *dev = (vfio_device_t *) priv;                                                                                               \
        vfio_log_op("[%08X:%04X] VFIO %s: msix_table_write" #length_char "(%08X, %0" #val_slength "X)\n", CS, cpu_state.pc, dev->name, addr, val); \
        uint16_t offset             = addr - dev->irq.msix.table_offset_precalc;                                                                   \
        dev->irq.msix.table[offset] = val;                                                                                                         \
        if ((offset & 0x000f) == 0x000c)                                                                                                           \
            vfio_irq_msix_updatemask(dev, offset);                                                                                                 \
    }                                                                                                                                              \
                                                                                                                                                   \
    static val_type                                                                                                                                \
        vfio_irq_msix_pba_read##length_char(uint32_t addr, void *priv)                                                                             \
    {                                                                                                                                              \
        vfio_device_t *dev = (vfio_device_t *) priv;                                                                                               \
        val_type       ret = dev->irq.msix.table[addr - dev->irq.msix.pba_offset_precalc];                                                         \
        vfio_log_op("[%08X:%04X] VFIO %s: msix_pba_read" #length_char "(%08X) = %0" #val_slength "X\n", CS, cpu_state.pc, dev->name, addr, ret);   \
        return ret;                                                                                                                                \
    }                                                                                                                                              \
                                                                                                                                                   \
    static void                                                                                                                                    \
        vfio_irq_msix_pba_write##length_char(uint32_t addr, val_type val, void *priv)                                                              \
    {                                                                                                                                              \
        vfio_device_t *dev = (vfio_device_t *) priv;                                                                                               \
        vfio_log_op("[%08X:%04X] VFIO %s: msix_pba_write" #length_char "(%08X, %0" #val_slength "X)\n", CS, cpu_state.pc, dev->name, addr, val);   \
    }

VFIO_RW_MSIX(b, uint8_t, 2)
VFIO_RW_MSIX(w, uint16_t, 4)
VFIO_RW_MSIX(l, uint32_t, 8)

static void
vfio_irq_disable(vfio_device_t *dev)
{
    /* Do nothing if IRQs are already disabled. */
    if (dev->irq.type == VFIO_PCI_NUM_IRQS)
        return;
    vfio_log("VFIO %s: irq_disable(%d)\n", dev->name, dev->irq.type);

    /* Pause IRQ thread. */
    thread_reset_event(irq_thread_resume);
    uint64_t val = 1;
    (void) !write(irq_thread_wake_fd, &val, sizeof(val));

    /* Always disable INTx after disabling MSI/MSI-X. */
    if (dev->irq.type == VFIO_PCI_MSIX_IRQ_INDEX)
        vfio_irq_msix_disable(dev);
    else if (dev->irq.type == VFIO_PCI_MSI_IRQ_INDEX)
        vfio_irq_msi_disable(dev);
    if (dev->irq.type == VFIO_PCI_INTX_IRQ_INDEX)
        vfio_irq_intx_disable(dev);

    /* Invalidate all IRQ vectors. */
    if (dev->irq.vectors) {
        for (int i = 0; i < dev->irq.vector_count; i++) {
            if (dev->irq.vectors[i].fd >= 0) {
                /* Remove eventfd from epoll. */
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, dev->irq.vectors[i].fd, NULL);
                close(dev->irq.vectors[i].fd);
            }
        }
        free(dev->irq.vectors);
        dev->irq.vectors      = NULL;
        dev->irq.vector_count = 0;
    }

    /* Resume IRQ thread. */
    thread_set_event(irq_thread_resume);
}

static void
vfio_irq_enable(vfio_device_t *dev, int type)
{
    /* Disable any existing IRQs. */
    vfio_irq_disable(dev);

    vfio_log("VFIO %s: irq_enable(%d)\n", dev->name, type);

    /* Determine the number of vectors needed. */
    switch (type) {
        case VFIO_PCI_INTX_IRQ_INDEX:
            /* Only one vector needed. */
            dev->irq.vector_count = 1;
            break;

        case VFIO_PCI_MSI_IRQ_INDEX:
            /* Up to the number of vectors read during init is needed. */
            dev->irq.vector_count = dev->irq.msi.vector_count;
            break;

        case VFIO_PCI_MSIX_IRQ_INDEX:
            /* The number of vectors read during init is needed. */
            dev->irq.vector_count = dev->irq.msix.vector_count;
            break;
    }

    /* Prepare structure for enabling the interrupt type. */
    struct vfio_irq_set irq_set = {
        .argsz = sizeof(irq_set) + (sizeof(int32_t) * dev->irq.vector_count),
        .flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER,
        .index = type,
        .start = 0,
        .count = dev->irq.vector_count
    };
    int32_t           *fd_list = (int32_t *) &irq_set.data;
    struct epoll_event event   = { .events = EPOLLIN };

    /* Create interrupt vectors with their respective eventfds. */
    dev->irq.vectors = (vfio_irq_t *) malloc(sizeof(vfio_irq_t) * dev->irq.vector_count);
    for (int i = 0; i < dev->irq.vector_count; i++) {
        dev->irq.vectors[i].dev    = dev;
        dev->irq.vectors[i].type   = type;
        dev->irq.vectors[i].vector = i;
        fd_list[i] = dev->irq.vectors[i].fd = eventfd(0, 0);
        if (fd_list[i] < 0)
            pclog("VFIO %s: IRQ eventfd %d failed (%d)\n", dev->name, i, errno);
        else {
            /* Add eventfd to epoll. */
            event.data.ptr = &dev->irq.vectors[i];
            epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd_list[i], &event);
        }
        dev->irq.vectors[i].msix_offset = i << 4; /* pre-calculated value to save operations on MSI-X processing */
    }

    /* Enable interrupt type on VFIO. */
    if (ioctl(dev->fd, VFIO_DEVICE_SET_IRQS, &irq_set))
        pclog("VFIO %s: SET_IRQS(%d, %d) failed (%d)\n", dev->name,
              type, dev->irq.vector_count, errno);
    dev->irq.type = type;
}

static void
vfio_region_init(vfio_device_t *dev, struct vfio_region_info *reg, vfio_region_t *region)
{
    /* Set region structure information. */
    region->fd     = dev->fd;
    region->offset = reg->offset;
    if (reg->index == VFIO_PCI_VGA_REGION_INDEX) {
        region->bar_id = 0xfe;
        if (region == &dev->vga_io_lo) {
            region->offset += 0x3b0;
            region->size = 12;
            region->type = 0x01;
        } else if (region == &dev->vga_io_hi) {
            region->offset += 0x3c0;
            region->size = 32;
            region->type = 0x01;
        } else {
            region->offset += 0xa0000;
            region->size = 131072;
            region->type = 0x00;
        }
    } else {
        region->size = reg->size;
        region->type = 0xff;
    }
    region->read  = !!(reg->flags & VFIO_REGION_INFO_FLAG_READ);
    region->write = !!(reg->flags & VFIO_REGION_INFO_FLAG_WRITE);
    region->dev   = dev;

    /* Use special memory mapping for expansion ROMs. */
    if (reg->index == VFIO_PCI_ROM_REGION_INDEX) {
        /* Use MMIO only. */
        region->fd = -1;

        /* Open ROM file if one was given. */
        FILE *fp = NULL;
        if (dev->rom_fn) {
            pclog("VFIO %s: Loading ROM from file: %s\n", dev->name, dev->rom_fn);
            fp = fopen(dev->rom_fn, "rb");
            if (fp) {
                /* Determine region size if the device has no ROM region. */
                if (!region->size) {
                    fseek(fp, 0, SEEK_END);
                    region->size = ceilpow2(ftell(fp));
                    if (region->size < 2048) /* minimum size for an expansion ROM */
                        region->size = 2048;
                    fseek(fp, 0, SEEK_SET);
                }
            } else {
                /* Fall back to the device's ROM if it has one. */
                pclog("VFIO %s: Could not read ROM file, ", dev->name);
                if (region->size) {
                    pclog("falling back to device ROM\n");
                } else {
                    /* Disable ROM. */
                    pclog("not enabling ROM\n");
                    region->read = region->write = 0;
                    goto end;
                }
            }
        }

        /* Mark this as the expansion ROM region. */
        region->type   = 0x00;
        region->bar_id = 0xff;

        /* Allocate ROM shadow area. */
        region->mmap_base = region->mmap_precalc = plat_mmap(region->size, 0);
        if (region->mmap_base == ((void *) -1)) {
            pclog("VFIO %s: ROM mmap(%" PRIu64 ") failed\n", dev->name, region->size);
            region->mmap_base = NULL;
            goto end;
        }
        memset(region->mmap_base, 0xff, region->size);

        int i, j = 0;
        if (fp) {
            /* Read ROM from file. */
            while ((i = fread(region->mmap_precalc, 1,
                              region->size - j,
                              fp))
                   != 0) {
                region->mmap_precalc += i;
                j += i;
            }
            fclose(fp);
        } else {
            /* Read ROM from device. */
            while ((i = pread(dev->fd, region->mmap_precalc,
                              region->size - j,
                              region->offset + j))
                   != 0) {
                region->mmap_precalc += i;
                j += i;
            }
        }

        /* Perform a few sanity checks on the ROM, starting with the signature. */
        j = 0;
        if (*((uint16_t *) &region->mmap_base[0x00]) == 0xaa55) {
            /* Check ROM length. */
            uint32_t rom_len = region->mmap_base[0x02] << 9; /* 512-byte blocks */
            if (rom_len > region->size) {
                pclog("VFIO %s: Warning: ROM length (%d bytes) is larger than ROM region (%" PRIu64 " bytes)\n",
                      dev->name, rom_len, region->size);
                j = 1;
            }

            /* Check PCI pointer. */
            uint16_t pci_ptr = *((uint16_t *) &region->mmap_base[0x18]);
            if (pci_ptr && (pci_ptr != 0xffff)) {
                /* Check PCI pointer bounds. */
                if (pci_ptr <= (region->size - 0x12)) {
                    /* Check PCI header ROM length only if <= 130048 bytes, as the
                       ROM length is 8 bits in the main header and 16 bits in here. */
                    uint32_t pci_len = *((uint16_t *) &region->mmap_base[pci_ptr + 0x18]) << 9; /* 512-byte blocks */
                    if ((pci_len <= (254 << 9)) && (pci_len != rom_len)) {
                        pclog("VFIO %s: Warning: ROM length in main header (%d bytes) is "
                              "different from length in PCI header (%d bytes)\n",
                              dev->name, rom_len, pci_len);
                        j = 1;
                    }
                } else {
                    pclog("VFIO %s: Warning: ROM has invalid PCI header pointer: %04X\n",
                          dev->name, pci_ptr);
                    j = 1;
                }
            } else {
                pclog("VFIO %s: Warning: ROM has no PCI header pointer\n",
                      dev->name);
                j = 1;
            }

            /* Compare checksum. */
            uint8_t checksum = 0;
            if (rom_len > region->size) /* don't go out of bounds */
                rom_len = region->size;
            rom_len -= 1;
            for (i = 0; i < rom_len; i++)
                checksum -= region->mmap_base[i];
            if (checksum != region->mmap_base[i]) {
                pclog("VFIO %s: Warning: ROM has bad checksum; expected %02X, got %02X\n",
                      dev->name, checksum, region->mmap_base[i]);
                j = 1;
            }
        } else {
            pclog("VFIO %s: Warning: ROM has no 55 AA signature\n", dev->name);
            j = 1;
        }

        /* Add a helpful reminder if a sanity check warning was printed
           and no ROM file was specified in this device's configuration. */
        if (j && !dev->rom_fn)
            pclog("VFIO %s: A custom ROM can be loaded with the _rom_fn directive.\n", dev->name);
    } else {
        /* Attempt to mmap the region. */
        region->mmap_base = mmap(NULL, region->size,
                                 (region->read ? PROT_READ : 0) | (region->write ? PROT_WRITE : 0),
                                 MAP_SHARED, region->fd, region->offset);
        if (region->mmap_base == ((void *) -1)) /* mmap failed */
            region->mmap_base = NULL;
    }
    region->mmap_precalc = region->mmap_base;

end:
    vfio_log("VFIO %s: Region: %s (offset %lX) (%d bytes) ", dev->name,
             region->name, region->offset, region->size);

    /* Create memory mapping for if we need it. */
    if (region->mmap_base) { /* mmap available */
        vfio_log("(MM)");
        mem_mapping_add(&region->mem_mapping, 0, 0,
                        region->read ? vfio_mem_readb_mm : NULL,
                        region->read ? vfio_mem_readw_mm : NULL,
                        region->read ? vfio_mem_readl_mm : NULL,
                        region->write ? vfio_mem_writeb_mm : NULL,
                        region->write ? vfio_mem_writew_mm : NULL,
                        region->write ? vfio_mem_writel_mm : NULL,
                        NULL, MEM_MAPPING_EXTERNAL, region->mmap_precalc);
    } else if (region->fd >= 0) { /* mmap not available, but fd is */
        vfio_log("(FD)");
        mem_mapping_add(&region->mem_mapping, 0, 0,
                        region->read ? vfio_mem_readb_fd : NULL,
                        region->read ? vfio_mem_readw_fd : NULL,
                        region->read ? vfio_mem_readl_fd : NULL,
                        region->write ? vfio_mem_writeb_fd : NULL,
                        region->write ? vfio_mem_writew_fd : NULL,
                        region->write ? vfio_mem_writel_fd : NULL,
                        NULL, MEM_MAPPING_EXTERNAL, region);
    } else {
        vfio_log("(not mapped)");
    }

    vfio_log(" (%c%c)\n", region->read ? 'R' : '-', region->write ? 'W' : '-');
}

static void
vfio_region_close(vfio_device_t *dev, vfio_region_t *region)
{
    /* Stop if this region was not initialized. */
    if (!region->size)
        return;

    /* Unmap memory if mmap was available. */
    if (region->mmap_base)
        plat_munmap(region->mmap_base, region->size);
}

static vfio_group_t *
vfio_group_get(int id, uint8_t add)
{
    /* Look for an existing group. */
    vfio_group_t *group = first_group;
    while (group) {
        if (group->id == id)
            return group;
        else if (group->next)
            group = group->next;
        else
            break;
    }

    /* Don't add a group if told not to. */
    if (!add)
        return NULL;

    /* Add group if no matches were found. */
    if (group) {
        group->next = (vfio_group_t *) malloc(sizeof(vfio_group_t));
        group       = group->next;
    } else {
        group = first_group = (vfio_group_t *) malloc(sizeof(vfio_group_t));
    }
    memset(group, 0, sizeof(vfio_group_t));
    group->id = id;

    /* Open VFIO group. */
    char group_file[32];
    snprintf(group_file, sizeof(group_file), "/dev/vfio/%d", group->id);
    group->fd = open(group_file, O_RDWR);
    if (group->fd < 0) {
        pclog("VFIO: Group %d not found\n", group->id);
        goto end;
    }

    /* Check if the group is viable. */
    struct vfio_group_status group_status = { .argsz = sizeof(group_status) };
    if (ioctl(group->fd, VFIO_GROUP_GET_STATUS, &group_status)) {
        pclog("VFIO: Group %d GET_STATUS failed (%d)\n", group->id, errno);
        goto close_fd;
    } else if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        pclog("VFIO: Group %d not viable\n", group->id);
        goto close_fd;
    }

    /* Claim the group. */
    if (ioctl(group->fd, VFIO_GROUP_SET_CONTAINER, &container_fd)) {
        pclog("VFIO: Group %d SET_CONTAINER failed\n", group->id);
        goto close_fd;
    }

    goto end;

close_fd:
    close(group->fd);
    group->fd = -1;
end:
    return group;
}

static void
vfio_dev_prereset(vfio_device_t *dev)
{
    vfio_log("VFIO %s: prereset()\n", dev->name);

    /* Disable interrupts. */
    vfio_irq_disable(dev);

    /* Extra steps for devices with power management capability. */
    if (dev->pm_cap) {
        /* Make sure the device is in D0 state. */
        uint8_t pm_ctrl = vfio_config_readb(0, dev->pm_cap + 4, 1, dev),
                state   = pm_ctrl & 0x03;
        if (state) {
            pm_ctrl &= ~0x03;
            vfio_config_writeb(0, dev->pm_cap + 4, pm_ctrl, 1, dev);

            pm_ctrl = vfio_config_readb(0, dev->pm_cap + 4, 1, dev);
            state   = pm_ctrl & 0x03;
            if (state)
                vfio_log("VFIO %s: Device stuck in D%d state\n", dev->name, state);
        }

        /* Enable PM reset if the device supports it. */
        dev->can_pm_reset = !(pm_ctrl & 0x08);
    }

    /* Enable function-level reset if supported. */
    dev->can_flr_reset = (dev->pcie_cap && (vfio_config_readb(0, dev->pcie_cap + 7, 1, dev) & 0x10)) || (dev->af_cap && (vfio_config_readb(0, dev->af_cap + 3, 1, dev) & 0x02));

    /* Disable bus master, BARs, expansion ROM and VGA regions; also enable INTx. */
    vfio_config_writew(0, 0x04, 2, vfio_config_readw(0, 0x04, 2, dev) & ~0x0407, dev);
}

static void
vfio_dev_postreset(vfio_device_t *dev)
{
    vfio_log("VFIO %s: postreset()\n", dev->name);

    /* Enable INTx interrupts. MSI(-X) can be enabled by the OS later. */
    if (!closing)
        vfio_irq_enable(dev, VFIO_PCI_INTX_IRQ_INDEX);

    /* Reset BARs, whatever this does. */
    uint32_t val = 0;
    for (uint8_t i = 0x10; i < 0x28; i++)
        (void) !pwrite(dev->config.fd, &val, sizeof(val), dev->config.offset + i);
}

static int
vfio_dev_init(vfio_device_t *dev)
{
    vfio_log("VFIO %s: init()\n", dev->name);

    /* Grab device. */
    dev->fd = ioctl(current_group->fd, VFIO_GROUP_GET_DEVICE_FD, dev->name);
    if (dev->fd < 0) {
        vfio_log("VFIO %s: GET_DEVICE_FD failed (%d)\n", dev->name, errno);
        goto end;
    }

    /* Get device information. */
    struct vfio_device_info device_info = { .argsz = sizeof(device_info) };
    if (ioctl(dev->fd, VFIO_DEVICE_GET_INFO, &device_info)) {
        pclog("VFIO %s: GET_INFO failed (%d), check for error in kernel log\n", dev->name, errno);
        goto end;
    }

    /* Check if any regions were returned. */
    if (!device_info.num_regions) {
        pclog("VFIO %s: No regions returned, check for error in kernel log\n", dev->name);
        goto end;
    }

    /* Set main reset flag. */
    dev->can_reset = !!(device_info.flags & VFIO_DEVICE_FLAGS_RESET);

    /* Establish region names. */
    for (uint8_t i = 0; i < 6; i++) {
        sprintf(dev->bars[i].name, "BAR #%d", dev->bars[i].bar_id = i);
        dev->bars[i].type = 0xff;
    }
    strcpy(dev->rom.name, "Expansion ROM");
    strcpy(dev->config.name, "Configuration space");
    strcpy(dev->vga_io_lo.name, "VGA MDA");
    strcpy(dev->vga_io_hi.name, "VGA CGA/EGA");
    strcpy(dev->vga_mem.name, "VGA Framebuffer");

    /* Initialize all regions. */
    struct vfio_region_info reg = { .argsz = sizeof(reg) };
    uint8_t                 cls;
    for (int i = 0; i < device_info.num_regions; i++) {
        /* Get region information. */
        reg.index = i;
        ioctl(dev->fd, VFIO_DEVICE_GET_REGION_INFO, &reg);

        /* Move on to the next region if this one is not valid. */
        if (!reg.size)
            continue;

        /* Initialize region according to its type. */
        switch (reg.index) {
            case VFIO_PCI_BAR0_REGION_INDEX ... VFIO_PCI_BAR5_REGION_INDEX:
                vfio_region_init(dev, &reg, &dev->bars[reg.index - VFIO_PCI_BAR0_REGION_INDEX]);
                if (reg.size)
                    dev->bar_count++;
                break;

            case VFIO_PCI_ROM_REGION_INDEX:
                vfio_region_init(dev, &reg, &dev->rom);
                break;

            case VFIO_PCI_CONFIG_REGION_INDEX:
                vfio_region_init(dev, &reg, &dev->config);
                break;

            case VFIO_PCI_VGA_REGION_INDEX:
                /* Don't initialize VGA region if this is not a video card. */
                if ((dev->config.fd > 0) && (pread(dev->config.fd, &cls, sizeof(cls), dev->config.offset + 0x0b) == sizeof(cls)) && (cls != 0x03))
                    break;

                vfio_region_init(dev, &reg, &dev->vga_io_lo); /* I/O [3B0:3BB] */
                vfio_region_init(dev, &reg, &dev->vga_io_hi); /* I/O [3C0:3DF] */
                vfio_region_init(dev, &reg, &dev->vga_mem);   /* memory [A0000:BFFFF] */

                /* Inform that a PCI VGA video card is attached if no video card is emulated. */
                if (gfxcard[0] == VID_NONE)
                    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_default);
                break;

            default:
                vfio_log("VFIO %s: Unknown region %d (offset %lX) (%d bytes) (%c%c)\n",
                         dev->name, reg.index, reg.offset, reg.size,
                         (reg.flags & VFIO_REGION_INFO_FLAG_READ) ? 'R' : '-',
                         (reg.flags & VFIO_REGION_INFO_FLAG_WRITE) ? 'W' : '-');
                break;
        }
    }

    /* Make sure we have a valid device. */
    if (!dev->config.fd || !dev->config.read) {
        pclog("VFIO %s: No configuration space region\n", dev->name);
        goto end;
    }

    /* Initialize ROM region if the device doesn't have one and we're loading a ROM from file. */
    if (dev->rom_fn && !dev->rom.fd) {
        reg.index  = VFIO_PCI_ROM_REGION_INDEX;
        reg.offset = reg.size = 0;
        reg.flags             = VFIO_REGION_INFO_FLAG_READ;
        vfio_region_init(dev, &reg, &dev->rom);
    }

    /* Go through PCI capability list if the device declares one. */
    dev->irq.msix.table_bar = dev->irq.msix.pba_bar = 0x07;
    uint8_t cap_ptr;
    uint8_t cap_id;
    if ((pread(dev->config.fd, &cap_ptr, sizeof(cap_ptr), dev->config.offset + 0x06) == sizeof(cap_ptr)) && (cap_ptr & 0x10)) {
        vfio_log("VFIO %s: Device capabilities:", dev->name);

        /* Read pointer to the first capability. */
        if (pread(dev->config.fd, &cap_ptr, sizeof(cap_ptr), dev->config.offset + 0x34) != sizeof(cap_ptr))
            cap_ptr = 0;
        while (cap_ptr && (cap_ptr != 0xff)) { /* check 0xff just in case */
            /* Read capability ID, and store pointers to ones we care about. */
            if (pread(dev->config.fd, &cap_id, sizeof(cap_id), dev->config.offset + cap_ptr) != sizeof(cap_id))
                cap_id = 0;
            switch (cap_id) {
                case 0x01:
                    vfio_log(" PM");
                    dev->pm_cap = cap_ptr;
                    break;

                case 0x05:
                    vfio_log(" MSI");
                    if (dev->msi_cap) /* multiple copies not permitted by spec */
                        break;
                    dev->msi_cap = cap_ptr;

                    /* Read control register. */
                    if (pread(dev->config.fd, &dev->irq.msi.ctl, sizeof(dev->irq.msi.ctl),
                              dev->config.offset + dev->msi_cap + 2)
                        != sizeof(dev->irq.msi.ctl))
                        dev->irq.msi.ctl = 0;

                    /* Set vector count. */
                    dev->irq.msi.vector_count = (dev->irq.msi.ctl >> 1) & 0x07;
                    break;

                case 0x10:
                    vfio_log(" PCIe");
                    dev->pcie_cap = cap_ptr;
                    break;

                case 0x11:
                    vfio_log(" MSI-X");
                    if (dev->msix_cap) /* multiple copies not permitted by spec */
                        break;
                    dev->msix_cap = cap_ptr;

                    /* Read control register. */
                    if (pread(dev->config.fd, &dev->irq.msix.ctl, sizeof(dev->irq.msix.ctl),
                              dev->config.offset + dev->msix_cap + 2)
                        != sizeof(dev->irq.msix.ctl))
                        dev->irq.msix.ctl = 0;

                    /* Set vector count. */
                    dev->irq.msix.vector_count = (dev->irq.msix.ctl & 0x07ff) + 1;

                    /* Read table and PBA BARs and offsets. */
                    if (pread(dev->config.fd, &dev->irq.msix.table_offset, sizeof(dev->irq.msix.table_offset),
                              dev->config.offset + dev->msix_cap + 4)
                        != sizeof(dev->irq.msix.table_offset))
                        dev->irq.msix.table_offset = 0x00000007;
                    dev->irq.msix.table_bar = dev->irq.msix.table_offset & 0x00000007;
                    dev->irq.msix.table_offset &= 0xfffffff8;

                    if (pread(dev->config.fd, &dev->irq.msix.pba_offset, sizeof(dev->irq.msix.pba_offset),
                              dev->config.offset + dev->msix_cap + 8)
                        != sizeof(dev->irq.msix.pba_offset))
                        dev->irq.msix.pba_offset = 0x00000007;
                    dev->irq.msix.pba_bar = dev->irq.msix.pba_offset & 0x00000007;
                    dev->irq.msix.pba_offset &= 0xfffffff8;

                    /* Allocate table and PBA structures. */
                    dev->irq.msix.table_size = dev->irq.msix.vector_count << 4;
                    dev->irq.msix.table      = malloc(dev->irq.msix.table_size);
                    if (!dev->irq.msix.table) {
                        pclog("VFIO %s: MSI-X table malloc(%d) failed\n", dev->name, dev->irq.msix.table_size);
                        dev->irq.msix.table_size = dev->irq.msix.vector_count = 0;
                    }

                    dev->irq.msix.pba_size = ((dev->irq.msix.vector_count - 1) >> 3) + 1;
                    dev->irq.msix.pba      = malloc(dev->irq.msix.pba_size);
                    if (!dev->irq.msix.pba) {
                        pclog("VFIO %s: MSI-X PBA malloc(%d) failed\n", dev->name, dev->irq.msix.pba_size);
                        dev->irq.msix.pba_size = dev->irq.msix.vector_count = 0;
                    }

                    /* Add table and PBA mappings.
                       Being added after region setup, they should override the main BAR mapping. */
                    mem_mapping_add(&dev->irq.msix.table_mapping, 0, 0,
                                    vfio_irq_msix_table_readb,
                                    vfio_irq_msix_table_readw,
                                    vfio_irq_msix_table_readl,
                                    vfio_irq_msix_table_writeb,
                                    vfio_irq_msix_table_writew,
                                    vfio_irq_msix_table_writel,
                                    NULL, MEM_MAPPING_EXTERNAL, dev);

                    mem_mapping_add(&dev->irq.msix.pba_mapping, 0, 0,
                                    vfio_irq_msix_pba_readb,
                                    vfio_irq_msix_pba_readw,
                                    vfio_irq_msix_pba_readl,
                                    vfio_irq_msix_pba_writeb,
                                    vfio_irq_msix_pba_writew,
                                    vfio_irq_msix_pba_writel,
                                    NULL, MEM_MAPPING_EXTERNAL, dev);
                    break;

                case 0x13:
                    vfio_log(" AF");
                    dev->af_cap = cap_ptr;
                    break;

                default:
                    vfio_log(" [%02X]", cap_id);
                    break;
            }

            /* Read pointer to the next capability. */
            if (pread(dev->config.fd, &cap_ptr, sizeof(cap_ptr), dev->config.offset + cap_ptr + 1) != sizeof(cap_ptr))
                cap_ptr = 0;
        }

        vfio_log("\n");
    }

    /* Read INTx IRQ pin. */
    vfio_irq_intx_setpin(dev);

    /* Add PCI card while mapping the configuration space. */
    pci_add_card(PCI_ADD_NORMAL, vfio_config_readb, vfio_config_writeb, dev, &dev->slot);

    return 0;

end:
    if (dev->fd >= 0)
        close(dev->fd);
    return 1;
}

static void
vfio_dev_close(vfio_device_t *dev)
{
    vfio_log("VFIO %s: close()\n", dev->name);

    /* Close all regions. */
    for (uint8_t i = 0; i < 6; i++)
        vfio_region_close(dev, &dev->bars[i]);
    vfio_region_close(dev, &dev->rom);
    vfio_region_close(dev, &dev->config);
    vfio_region_close(dev, &dev->vga_io_lo);
    vfio_region_close(dev, &dev->vga_io_hi);
    vfio_region_close(dev, &dev->vga_mem);

    /* Close device fd. */
    if (dev->fd >= 0) {
        close(dev->fd);
        dev->fd = -1;
    }

    /* Clean up. */
    if (dev->irq.msix.table)
        free(dev->irq.msix.table);
    if (dev->irq.msix.pba)
        free(dev->irq.msix.pba);
    free(dev->name);
}

void
vfio_unmap_dma(uint32_t offset, uint32_t size)
{
    struct vfio_iommu_type1_dma_unmap dma_unmap = {
        .argsz = sizeof(dma_unmap),
        .iova  = offset,
        .size  = size
    };

    vfio_log("VFIO: unmap_dma(%08X, %d)\n", offset, size);

    /* Unmap DMA region. */
    if (!ioctl(container_fd, VFIO_IOMMU_UNMAP_DMA, &dma_unmap))
        return;

    vfio_log("VFIO: unmap_dma(%08X, %d) failed (%d)\n", offset, size, errno);
}

void
vfio_map_dma(uint8_t *ptr, uint32_t offset, uint32_t size)
{
    struct vfio_iommu_type1_dma_map dma_map = {
        .argsz = sizeof(dma_map),
        .vaddr = (uint64_t) ptr,
        .iova  = offset,
        .size  = size,
        .flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE
    };

    vfio_log("VFIO: map_dma(%08X, %d)\n", offset, size);

    /* Map DMA region. */
    if (!ioctl(container_fd, VFIO_IOMMU_MAP_DMA, &dma_map))
        return;

    /* QEMU says mapping should be retried in case of EBUSY. */
    if (errno == EBUSY) {
        vfio_unmap_dma(offset, size);
        if (!ioctl(container_fd, VFIO_IOMMU_MAP_DMA, &dma_map))
            return;
    }

    pclog("VFIO: map_dma(%08X, %d) failed (%d)\n", offset, size, errno);
}

static void
vfio_reset(void *priv)
{
    vfio_log("VFIO: reset()\n");

    /* Pre-reset and figure out the reset type for all devices. */
    int                               size;
    int                               count;
    struct vfio_pci_hot_reset_info   *hot_reset_info;
    struct vfio_pci_dependent_device *devices;
    char                              name[13];
    vfio_group_t                     *group = first_group;
    vfio_device_t                    *dev;
    while (group) {
        dev = group->first_device;
        while (dev) {
            /* Pre-reset this device. */
            vfio_dev_prereset(dev);

            /* Clear hot reset capable flag for this device. */
            dev->can_hot_reset = 0;

            /* Get hot reset information for the first time to get the entry count. */
            size           = sizeof(struct vfio_pci_hot_reset_info);
            hot_reset_info = (struct vfio_pci_hot_reset_info *) malloc(size);
            if (!hot_reset_info) {
                vfio_log("VFIO %s: malloc(hot_reset_info) 1 failed\n", dev->name);
                goto next1;
            }
            memset(hot_reset_info, 0, size);
            hot_reset_info->argsz = size;
            if (ioctl(dev->fd, VFIO_DEVICE_GET_PCI_HOT_RESET_INFO, hot_reset_info) && (errno != ENOSPC)) {
                vfio_log("VFIO %s: GET_PCI_HOT_RESET_INFO 1 failed (%d)\n", dev->name, errno);
                goto next1;
            }
            count = hot_reset_info->count;
            free(hot_reset_info);

            /* Get hot reset information for the second time to get the actual entries. */
            size           = sizeof(struct vfio_pci_hot_reset) + (sizeof(struct vfio_pci_dependent_device) * count);
            hot_reset_info = (struct vfio_pci_hot_reset_info *) malloc(size);
            if (!hot_reset_info) {
                vfio_log("VFIO %s: malloc(hot_reset_info) 2 failed\n", dev->name);
                goto next1;
            }
            memset(hot_reset_info, 0, size);
            hot_reset_info->argsz = size;
            if (ioctl(dev->fd, VFIO_DEVICE_GET_PCI_HOT_RESET_INFO, hot_reset_info)) {
                vfio_log("VFIO %s: GET_PCI_HOT_RESET_INFO 2 failed (%d)\n", dev->name, errno);
                goto next1;
            }
            devices = &hot_reset_info->devices[0];

            /* Go through the dependent device entries. */
            for (int i = 0; i < count; i++) {
                /* Build this dependent device's name. */
                snprintf(name, sizeof(name), "%04x:%02x:%02x.%1x",
                         devices[i].segment, devices[i].bus,
                         PCI_SLOT(devices[i].devfn), PCI_FUNC(devices[i].devfn));

                /* Check if we own this device's group. */
                if (!vfio_group_get(devices[i].group_id, 0)) {
                    vfio_log("VFIO %s: Cannot hot reset; we don't own"
                             "group %d for dependent device %s\n",
                             dev->name, devices[i].group_id, name);
                    goto next1;
                }
            }

            /* Mark this device as hot reset capable. */
            dev->can_hot_reset = 1;

next1:
            if (hot_reset_info)
                free(hot_reset_info);
            dev = dev->next;
        }
        group = group->next;
    }

    /* Count the number of groups we own. */
    count = 0;
    group = first_group;
    while (group) {
        count++;
        group = group->next;
    }

    /* Allocate hot reset structure. */
    struct vfio_pci_hot_reset *hot_reset;
    size             = sizeof(struct vfio_pci_hot_reset) + (sizeof(int32_t) * count);
    hot_reset        = (struct vfio_pci_hot_reset *) calloc(1, size);
    hot_reset->argsz = size;
    int32_t *fds     = &hot_reset->group_fds[0];

    /* Add group fds. */
    group = first_group;
    while (group) {
        fds[hot_reset->count++] = group->fd;
        group                   = group->next;
    }

    /* Reset all devices. */
    group = first_group;
    while (group) {
        dev = group->first_device;
        while (dev) {
            /* Try function-level reset.
               I don't really understand the !pm_reset check, but QEMU does it. */
            if (dev->can_reset && (!dev->can_pm_reset || dev->can_flr_reset)) {
                if (ioctl(dev->fd, VFIO_DEVICE_RESET))
                    vfio_log("VFIO %s: DEVICE_RESET 1 failed (%d)\n", dev->name, errno);
                else {
                    vfio_log("VFIO %s: FLR reset successful\n", dev->name);
                    goto next2;
                }
            }

            /* Try hot reset. */
            if (dev->can_hot_reset) {
                if (ioctl(dev->fd, VFIO_DEVICE_PCI_HOT_RESET, hot_reset))
                    vfio_log("VFIO %s: PCI_HOT_RESET failed (%d)\n", dev->name, errno);
                else {
                    vfio_log("VFIO %s: Hot reset successful\n", dev->name);
                    goto next2;
                }
            }

            /* Try PM reset. */
            if (dev->can_reset && dev->can_pm_reset) {
                if (ioctl(dev->fd, VFIO_DEVICE_RESET))
                    vfio_log("VFIO %s: DEVICE_RESET 2 failed (%d)\n", dev->name, errno);
                else {
                    vfio_log("VFIO %s: PM reset successful\n", dev->name);
                    goto next2;
                }
            }

            /* Warn if no reset types were successful. */
            pclog("VFIO %s: Device was not reset!\n", dev->name);

next2:
            dev = dev->next;
        }
        group = group->next;
    }

    /* Clean up. */
    free(hot_reset);

    /* Post-reset all devices. */
    group = first_group;
    while (group) {
        dev = group->first_device;
        while (dev) {
            vfio_dev_postreset(dev);
            dev = dev->next;
        }
        group = group->next;
    }
}

void
vfio_init(void)
{
    vfio_log("VFIO: init()\n");

    /* Stay quiet if VFIO is not configured. */
    char *category = "VFIO",
         *devices  = config_get_string(category, "devices", NULL);
    if (!devices || !strlen(devices))
        return;

    /* Open VFIO container. */
    container_fd = open("/dev/vfio/vfio", O_RDWR);
    if (container_fd < 0) {
        pclog("VFIO: Container not found (is vfio-pci loaded?)\n");
        return;
    }

    /* Check VFIO API version. */
    int api = ioctl(container_fd, VFIO_GET_API_VERSION);
    if (api != VFIO_API_VERSION) {
        pclog("VFIO: Unknown API version %d (expected %d)\n", api, VFIO_API_VERSION);
        goto close_container;
    }

    /* Check for Type1 IOMMU support. */
    if (!ioctl(container_fd, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU)) {
        pclog("VFIO: Type1 IOMMU not supported\n");
        goto close_container;
    }

    /* Parse device list. */
    char          *strtok_save;
    char          *token = strtok_r(devices, " ", &strtok_save);
    char          *p;
    char          *dev_name;
    char          *sysfs_device;
    char          *config_key;
    int            i;
    int            domain_id;
    int            bus_id;
    int            dev_id;
    int            func_id;
    vfio_device_t *dev = NULL;
    vfio_device_t *prev_dev;
    vfio_group_t  *group;
    while (token) {
        /* Determine if the device was specified by location or sysfs path. */
        dev_name = NULL;
        if (token[0] == '/') {
            /* sysfs path: use basename as device name. */
            i        = strlen(token);
            dev_name = malloc(i + 1);
            strncpy(dev_name, path_get_basename(token), i);

            /* Just append iommu_group to the path. */
            sysfs_device = malloc(i + 13);
            snprintf(sysfs_device, i + 13,
                     "%s/iommu_group", token);
        } else if (token[0]) {
            /* Location: read domain/bus/device/function. */
            i = sscanf(token, "%x:%x:%x.%x", &domain_id, &bus_id, &dev_id, &func_id);
            if (i < 3) {
                domain_id = 0;
                i         = sscanf(token, "%x:%x.%x", &bus_id, &dev_id, &func_id);
                if (i < 2) {
                    bus_id = 0;
                    i      = sscanf(token, "%x.%x", &dev_id, &func_id);
                    if (i < 1) {
                        pclog("VFIO: Invalid device location: %s\n", token);
                        goto next;
                    } else if (i == 1) {
                        func_id = 0;
                    }
                } else if (i == 2) {
                    func_id = 0;
                }
            } else if (i == 3) {
                func_id = 0;
            }

            /* Use dddd:bb:dd.f as device name. */
            dev_name = malloc(13);
            snprintf(dev_name, 13,
                     "%04x:%02x:%02x.%1x", domain_id, bus_id, dev_id, func_id);

            /* Generate sysfs path. */
            sysfs_device = malloc(46);
            snprintf(sysfs_device, 46,
                     "/sys/bus/pci/devices/%s/iommu_group", dev_name);
        } else {
            /* Skip blank token. */
            goto next;
        }

        pclog("VFIO %s: IOMMU group ", dev_name);

        p = realpath(sysfs_device, NULL);
        free(sysfs_device);
        if (p) {
            /* Parse group ID. */
            if (sscanf(path_get_basename(p), "%d", &i) != 1) {
                pclog("path could not be parsed: %s\n", p);
                free(p);
                goto next;
            }

            pclog("%d\n", i);
            free(p);
        } else {
            /* No symlink found, move on to the next device. */
            pclog("not found (%d)\n", errno);
            goto next;
        }

        /* Get group by ID, and move on to the next device
           if the group failed to initialize. (Not viable, etc.) */
        group = vfio_group_get(i, 1);
        if (group->fd < 0) {
            pclog("VFIO %s: Skipping because group failed to initialize\n", dev_name);
            goto next;
        }

        /* Allocate device structure. */
        prev_dev = group->current_device;
        dev = group->current_device = (vfio_device_t *) calloc(1, sizeof(vfio_device_t));

        /* Initialize device structure. */
        dev->name     = dev_name;
        dev_name      = NULL; /* don't free it further down */
        dev->irq.type = VFIO_PCI_NUM_IRQS;

        /* Read device-specific settings. */
        i          = strlen(token) + 8;
        config_key = malloc(i);
        snprintf(config_key, i, "%s_rom_fn", token);
        dev->rom_fn = config_get_string(category, config_key, NULL);
        free(config_key);

        /* Add to linked device list. */
        if (prev_dev)
            prev_dev->next = dev;
        else
            group->first_device = dev;

next: /* Clean up. */
        if (dev_name)
            free(dev_name);

        /* Read next device name. */
        token = strtok_r(NULL, " ", &strtok_save);
    }

    /* Stop if no devices were added. */
    if (!dev)
        goto close_container;

    /* Set IOMMU type. */
    if (ioctl(container_fd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU)) {
        pclog("VFIO: SET_IOMMU failed (%d)\n", errno);
        goto close_container;
    }

    /* Map RAM to container for DMA. */
    vfio_map_dma(ram, 0, 1024UL * MIN(mem_size, 1048576));
    if (ram2)
        vfio_map_dma(ram2, 1024UL * 1048576, 1024UL * (mem_size - 1048576));

    /* Initialize epoll. */
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        pclog("VFIO: epoll_create1 failed (%d)\n", errno);
        goto close_container;
    }

    /* Initialize IRQ thread wake eventfd. */
    irq_thread_wake_fd = eventfd(0, 0);
    if (irq_thread_wake_fd <= 0) {
        pclog("VFIO: eventfd failed (%d)\n", errno);
        goto close_container;
    }
    struct epoll_event event = { .events = EPOLLIN };
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, irq_thread_wake_fd, &event) < 0) {
        pclog("VFIO: EPOLL_CTL_ADD failed (%d)\n", errno);
        goto close_container;
    }

    /* Initialize and start IRQ thread. */
    irq_event         = thread_create_event();
    irq_thread_resume = thread_create_event();
    thread_set_event(irq_thread_resume);
    irq_thread = thread_create(vfio_irq_thread, NULL);

    /* Start IRQ timer. */
    timer_add(&irq_timer, vfio_irq_timer, NULL, 0);
    vfio_irq_timer(NULL);

    /* Initialize all devices. */
    current_group = first_group;
    while (current_group) {
        prev_dev = NULL;
        dev      = current_group->first_device;
        while (dev) {
            current_group->current_device = dev;
            if (vfio_dev_init(dev)) {
                pclog("VFIO %s: dev_init failed\n", dev->name);

                /* Deallocate this device if initialization failed. */
                if (prev_dev)
                    prev_dev->next = dev->next;
                else
                    current_group->first_device = dev->next;
                dev = dev->next;
                free(current_group->current_device);
                continue;
            }
            prev_dev = dev;
            dev      = dev->next;
        }
        current_group = current_group->next;
    }

    /* Reset all devices. */
    vfio_log("VFIO: Performing initial reset\n");
    closing = 0;

    /* Add device_t to keep track of reset and close. */
    device_add(&vfio_device);

close_container:
    close(container_fd);
    container_fd = -1;
}

void
vfio_close(void *priv)
{
    vfio_log("VFIO: close()\n");

    /* Reset all devices. */
    closing = 1;
    vfio_reset(priv);

    /* Stop IRQ timer. */
    timer_on_auto(&irq_timer, 0.0);

    /* Stop IRQ thread by closing the epoll fd. */
    if (epoll_fd >= 0) {
        close(epoll_fd);
        epoll_fd = -1;
    }
    thread_set_event(irq_thread_resume);

    /* Close all groups. */
    while (first_group) {
        current_group = first_group;

        /* Close all devices. */
        while (current_group->first_device) {
            current_group->current_device = current_group->first_device;

            /* Close device. */
            vfio_dev_close(current_group->current_device);

            /* Deallocate device. */
            current_group->first_device = current_group->current_device->next;
            free(current_group->current_device);
        }

        /* Close group fd. */
        if (current_group->fd >= 0)
            close(current_group->fd);

        /* Deallocate group. */
        first_group = current_group->next;
        free(current_group);
    }

    /* Close container. */
    if (container_fd >= 0) {
        close(container_fd);
        container_fd = -1;
    }
}

static void
vfio_speed_changed(void *priv)
{
    /* Set operation timings. */
    timing_readb  = (int) (pci_timing * timing_default.read_b);
    timing_readw  = (int) (pci_timing * timing_default.read_w);
    timing_readl  = (int) (pci_timing * timing_default.read_l);
    timing_writeb = (int) (pci_timing * timing_default.write_b);
    timing_writew = (int) (pci_timing * timing_default.write_w);
    timing_writel = (int) (pci_timing * timing_default.write_l);
}

static const device_t vfio_device = {
    .name          = "VFIO PCI Passthrough",
    .internal_name = "vfio",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = NULL,
    .close         = vfio_close,
    .reset         = vfio_reset,
    .available     = NULL,
    .speed_changed = vfio_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};
