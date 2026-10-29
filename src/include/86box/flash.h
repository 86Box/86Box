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
 * A JEDEC flash chip (SST, Winbond, AMD) that is not tied to the boot BIOS,
 * for machines that map their own chips. The model is one of the
 * sst_flash_*, winbond_flash_* or amd_flash_* descriptors below, which is
 * only used to describe the chip and is not added as a device.
 */
typedef struct sst_t sst_t;

typedef struct sst_flash_cfg_t {
    const char    *nvr_name; /* Backing file name without ".bin", NULL for none. */
    const uint8_t *image;    /* Initial contents, overridden by the backing file. */
    uint32_t       image_len;
    uint32_t       size; /* Decoded size if smaller than the chip, 0 for all of it. */
} sst_flash_cfg_t;

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

extern const device_t intel_flash_e28f0xx_device;
extern const device_t intel_flash_e28f0xx_cobalt3k_device;

extern void flash_e28f0xx_cobalt3k_update(uint8_t val);

extern const device_t amd_flash_am29f016d_device;

extern const device_t amd_am28f010_flash_device;
extern const device_t catalyst_flash_device;

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

#endif /*EMU_FLASH_H*/
