/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Handling of the emulated flash devices.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *          Nathan Lewis, <git@nrlewis.dev>
 *
 *          Copyright 2020      Miran Grca.
 *          Copyright 2022-2023 Jasmine Iwanek.
 *          Copyright 2026      Nathan Lewis.
 */
#ifndef EMU_FLASH_H
#define EMU_FLASH_H

#define AMD_FLAG_LEGACY           0x01

struct _mem_mapping_;

/*
 * The flash chips can be used apart from the boot BIOS, by machines that
 * map their own chips. Each family has the same calls: create a chip from
 * one of its descriptors below, which then only describes the chip and is
 * not added as a device, map windows of it, and destroy it, which saves it
 * to its backing file. A window bigger than the chip repeats the chip, and
 * starts on a multiple of the chip's size.
 */
typedef struct flash_cfg_t {
    const char    *nvr_name; /* Backing file name without ".bin", NULL for none. */
    const uint8_t *image;    /* Initial contents, overridden by the backing file. */
    uint32_t       image_len;
    uint32_t       size; /* Decoded size if smaller than the chip, 0 for all of it. */
} flash_cfg_t;

/* JEDEC chips (SST, Winbond, AMD): the sst_flash_*, winbond_flash_* and amd_flash_29f* descriptors. */
typedef struct sst_t sst_t;

typedef flash_cfg_t sst_flash_cfg_t;

extern sst_t   *sst_flash_create(const device_t *model, const sst_flash_cfg_t *cfg);
extern void     sst_flash_destroy(sst_t *dev);
extern void     sst_flash_reset(sst_t *dev);
extern void     sst_flash_map(sst_t *dev, struct _mem_mapping_ *mapping, uint32_t base, uint32_t size);
extern void     sst_flash_remap(sst_t *dev, struct _mem_mapping_ *mapping, uint32_t base, uint32_t size);
extern void     sst_flash_write(sst_t *dev, uint32_t addr, uint8_t val);
extern void     sst_flash_set_write_protect(sst_t *dev, int protect);
extern uint32_t sst_flash_get_size(const sst_t *dev);

/* The BIOS chip select. A chipset that decodes the flash's chip select can
   refuse to assert it for writes -- the ESC's BIOSCSB bit 3, BIOS Write
   Enable -- and then the part never sees the cycle. NULL, as on every
   machine that does not set it, is a chip select always asserted. */
extern int  (*flash_bios_write_gate)(uint32_t addr, void *priv);
extern void  *flash_bios_write_gate_priv;
extern int    flash_bios_write_selected(uint32_t addr);

/* The same chip select for reads: a range BIOSCSA/B does not enable is not
   decoded at all, and the bus reads as open (FFh) there. */
extern int  (*flash_bios_read_gate)(uint32_t addr, void *priv);
extern void  *flash_bios_read_gate_priv;
extern int    flash_bios_read_selected(uint32_t addr);

/* Code is fetched straight from a mapping's exec pointer, past the read
   handlers, so a flash registers a hook that the chipset calls whenever the
   chip select changes; the hook passes each mapping and its array pointer
   to flash_bios_mapping_update(), which keeps the exec pointer only while
   the whole mapping is selected. */
extern void   flash_bios_set_decode_hook(void (*hook)(void *priv), void *priv);
extern void   flash_bios_decode_changed(void);
extern void   flash_bios_mapping_update(mem_mapping_t *map, uint8_t *exec);

/*
 * Intel 28F004S5/28F008S5/28F016S5 Smart Flash chips: the
 * intel_flash_e28f0xx descriptors. The size picks the part, so it is
 * required.
 */
typedef struct e28f0xx_t e28f0xx_t;

extern e28f0xx_t *e28f0xx_create(const device_t *model, const flash_cfg_t *cfg);
extern void       e28f0xx_destroy(e28f0xx_t *dev);
extern void       e28f0xx_reset(e28f0xx_t *dev);
extern void       e28f0xx_map(e28f0xx_t *dev, struct _mem_mapping_ *mapping, uint32_t base, uint32_t size);
extern void       e28f0xx_remap(e28f0xx_t *dev, struct _mem_mapping_ *mapping, uint32_t base, uint32_t size);
extern void       e28f0xx_set_write_protect(e28f0xx_t *dev, int protect);
extern uint32_t   e28f0xx_get_size(const e28f0xx_t *dev);

extern const device_t intel_flash_e28f0xx_device;
extern const device_t intel_flash_e28f0xx_cobalt3k_device;

extern void flash_e28f0xx_cobalt3k_update(uint8_t val);

/* AMD Am29F016D chips: amd_flash_am29f016d_device. */
typedef struct am29f016d_t am29f016d_t;

extern am29f016d_t *am29f016d_create(const device_t *model, const flash_cfg_t *cfg);
extern void         am29f016d_destroy(am29f016d_t *dev);
extern void         am29f016d_reset(am29f016d_t *dev);
extern void         am29f016d_map(am29f016d_t *dev, struct _mem_mapping_ *mapping, uint32_t base, uint32_t size);
extern void         am29f016d_remap(am29f016d_t *dev, struct _mem_mapping_ *mapping, uint32_t base, uint32_t size);
extern void         am29f016d_set_write_protect(am29f016d_t *dev, int protect);
extern uint32_t     am29f016d_get_size(const am29f016d_t *dev);

extern const device_t amd_flash_am29f016d_device;

/* Catalyst 28F010/28F020 and AMD Am28F010 chips: catalyst_flash_device,
   catalyst_28f020_flash_device and amd_am28f010_flash_device. */
typedef struct catalyst_t catalyst_t;

extern catalyst_t *catalyst_flash_create(const device_t *model, const flash_cfg_t *cfg);
extern void        catalyst_flash_destroy(catalyst_t *dev);
extern void        catalyst_flash_reset(catalyst_t *dev);
extern void        catalyst_flash_map(catalyst_t *dev, struct _mem_mapping_ *mapping, uint32_t base, uint32_t size);
extern void        catalyst_flash_remap(catalyst_t *dev, struct _mem_mapping_ *mapping, uint32_t base, uint32_t size);
extern void        catalyst_flash_set_write_protect(catalyst_t *dev, int protect);
extern uint32_t    catalyst_flash_get_size(const catalyst_t *dev);

/* For a board that drives some of the boot BIOS chip's address lines itself,
   such as a bank select: the lines in mask take the levels in val. */
extern void catalyst_flash_bios_set_addr_lines(void *priv, uint32_t mask, uint32_t val);

extern const device_t amd_am28f010_flash_device;
extern const device_t catalyst_flash_device;
extern const device_t catalyst_28f020_flash_device;

/*
 * Intel 28F001BX/28F002BX/28F004BX boot block chips and their Micron
 * equivalents: the intel_flash_* and micron_flash_* descriptors. One
 * descriptor covers the 128k, 256k and 512k parts, so the size is required.
 * On the AMI part A16 is inverted; its image is what the CPU sees.
 */
typedef struct intel_flash_t intel_flash_t;

extern intel_flash_t *intel_flash_create(const device_t *model, const flash_cfg_t *cfg);
extern void           intel_flash_destroy(intel_flash_t *dev);
extern void           intel_flash_reset(intel_flash_t *dev);
extern void           intel_flash_map(intel_flash_t *dev, struct _mem_mapping_ *mapping, uint32_t base, uint32_t size);
extern void           intel_flash_remap(intel_flash_t *dev, struct _mem_mapping_ *mapping, uint32_t base, uint32_t size);
extern void           intel_flash_set_write_protect(intel_flash_t *dev, int protect);
extern uint32_t       intel_flash_get_size(const intel_flash_t *dev);

extern const device_t intel_flash_bxt_ami_device;
extern const device_t intel_flash_bxt_device;
extern const device_t intel_flash_bxb_device;

extern const device_t micron_flash_t_device;
extern const device_t micron_flash_x00_t_device;

#define SST_FORCE_128    0x80000000

extern void sst_set_xor(int enable);

extern const device_t sst_flash_29ee010_device;
extern const device_t sst_flash_29ee020_device;

extern const device_t winbond_flash_w29c512_device;
extern const device_t winbond_flash_w29c010_device;
extern const device_t winbond_flash_w29c011a_device;
extern const device_t winbond_flash_w29c020_device;
extern const device_t winbond_flash_w29c040_device;

extern const device_t sst_flash_39sf512_device;
extern const device_t sst_flash_39sf010_device;
extern const device_t sst_flash_39sf020_device;
extern const device_t sst_flash_39sf040_device;

extern const device_t sst_flash_39lf512_device;
extern const device_t sst_flash_39lf010_device;
extern const device_t sst_flash_39lf020_device;
extern const device_t sst_flash_39lf040_device;
extern const device_t sst_flash_39lf080_device;
extern const device_t sst_flash_39lf016_device;

extern const device_t sst_flash_49lf002_device;
extern const device_t sst_flash_49lf020_device;
extern const device_t sst_flash_49lf020a_device;
extern const device_t sst_flash_49lf003_device;
extern const device_t sst_flash_49lf030_device;
extern const device_t sst_flash_49lf004_device;
extern const device_t sst_flash_49lf004c_device;
extern const device_t sst_flash_49lf040_device;
extern const device_t sst_flash_49lf008_device;
extern const device_t sst_flash_49lf008c_device;
extern const device_t sst_flash_49lf080_device;
extern const device_t sst_flash_49lf016_device;
extern const device_t sst_flash_49lf160_device;

extern const device_t amd_flash_29f010a_device;
extern const device_t amd_flash_29f020a_device;
extern const device_t amd_flash_29f002nbt_device;
extern const device_t amd_flash_29f040a_device;
extern const device_t amd_flash_29f080a_device;

#endif /*EMU_FLASH_H*/
