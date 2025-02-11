/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the common disk controller handler.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2017-2020 Fred N. van Kempen.
 */
#ifndef EMU_HDC_H
#define EMU_HDC_H

#define MFM_NUM   2  /* 2 drives per controller supported */
#define ESDI_NUM  2  /* 2 drives per controller supported */
#define XTA_NUM   2  /* 2 drives per controller supported */

/* Controller types. */
#define HDC_NONE     0
#define HDC_INTERNAL 1

#define HDC_MAX 2

extern int hdc_current[HDC_MAX];

extern const device_t st506_xt_xebec_device;              /* st506_xt_xebec */
extern const device_t st506_xt_wdxt_gen_device;           /* st506_xt_wdxt_gen */
extern const device_t st506_xt_dtc5150x_device;           /* st506_xt_dtc */
extern const device_t st506_xt_st11_m_device;             /* st506_xt_st11_m */
extern const device_t st506_xt_st11_r_device;             /* st506_xt_st11_m */
extern const device_t st506_xt_wd1002a_wx1_device;        /* st506_xt_wd1002a_wx1 */
extern const device_t st506_xt_wd1002a_wx1_nobios_device; /* st506_xt_wd1002a_wx1 */
extern const device_t st506_xt_wd1002a_27x_device;        /* st506_xt_wd1002a_27x */
extern const device_t st506_at_wd1003_device;             /* st506_at_wd1003 */
extern const device_t st506_xt_wd1004a_wx1_device;        /* st506_xt_wd1004a_wx1 */
extern const device_t st506_xt_wd1004_27x_device;         /* st506_xt_wd1004_27x */
extern const device_t st506_xt_wd1004a_27x_device;        /* st506_xt_wd1004a_27x */
extern const device_t st506_xt_victor_v86p_device;        /* st506_xt_victor_v86p */
extern const device_t st506_xt_toshiba_t1200_device;      /* st506_xt_toshiba_t1200 */

extern const device_t esdi_at_wd1007vse1_device; /* esdi_at */
extern const device_t esdi_ps2_device;           /* esdi_mca */

extern const device_t ide_isa_device;         /* isa_ide */
extern const device_t ide_isa_sec_device;     /* isa_ide sec*/
extern const device_t ide_isa_2ch_device;     /* isa_ide_2ch */
extern const device_t ide_vlb_device;         /* vlb_ide */
extern const device_t ide_vlb_sec_device;     /* vlb_ide sec */
extern const device_t ide_vlb_2ch_device;     /* vlb_ide_2ch */
extern const device_t ide_pci_device;         /* pci_ide */
extern const device_t ide_pci_sec_device;     /* pci_ide sec */
extern const device_t ide_pci_2ch_device;     /* pci_ide_2ch */

extern const device_t ide_ali1489_device;                       /* ALi M1489 */
extern const device_t ide_ali5213_device;                       /* ALi M5213 */

extern const device_t ide_cmd640_vlb_device;                    /* CMD PCI-640B VLB */
extern const device_t ide_cmd640_vlb_178_device;                /* CMD PCI-640B VLB (Port 178h) */
extern const device_t ide_cmd640_vlb_pri_device;                /* CMD PCI-640B VLB (Only primary channel) */
extern const device_t ide_cmd640_vlb_pri_178_device;            /* CMD PCI-640B VLB (Only primary channel) (Port 178h) */
extern const device_t ide_cmd640_vlb_sec_device;                /* CMD PCI-640B VLB (Only secondary channel) */
extern const device_t ide_cmd640_vlb_sec_178_device;            /* CMD PCI-640B VLB (Only secondary channel) (Port 178h) */
extern const device_t ide_cmd640_pci_device;                    /* CMD PCI-640B PCI */
extern const device_t ide_cmd640_pci_legacy_only_device;        /* CMD PCI-640B PCI (Legacy Mode Only) */
extern const device_t ide_cmd640_pci_single_channel_device;     /* CMD PCI-640B PCI (Only primary channel) */
extern const device_t ide_cmd640_pci_single_channel_sec_device; /* CMD PCI-640B PCI (Only secondary channel) */
extern const device_t ide_cmd646_device;                        /* CMD PCI-646 */
extern const device_t ide_cmd646_legacy_only_device;            /* CMD PCI-646 (Legacy Mode Only) */
extern const device_t ide_cmd646_single_channel_device;         /* CMD PCI-646 (Only primary channel) */

extern const device_t ide_opti611_vlb_device;                   /* OPTi 82c611/611A VLB */
extern const device_t ide_opti611_vlb_sec_device;               /* OPTi 82c611/611A VLB (Secondary channel) */

extern const device_t ide_um8673f_device;                       /* UMC UM8673F */
extern const device_t ide_um8886af_device;                      /* UMC UM8886AF */

extern const device_t ide_w83769f_vlb_device;                   /* Winbond W83769F VLB */
extern const device_t ide_w83769f_vlb_34_device;                /* Winbond W83769F VLB (Port 34h) */
extern const device_t ide_w83769f_pci_device;                   /* Winbond W83769F PCI */
extern const device_t ide_w83769f_pci_34_device;                /* Winbond W83769F PCI (Port 34h) */
extern const device_t ide_w83769f_pci_single_channel_device;    /* Winbond W83769F PCI (Only primary channel) */

extern const device_t ide_ter_device;
extern const device_t ide_ter_pnp_device;
extern const device_t ide_qua_device;
extern const device_t ide_qua_pnp_device;

extern const device_t mcide_device;

extern const device_t xta_wdxt150_device; /* xta_wdxt150 */
extern const device_t xta_hd20_device;    /* EuroPC internal */

extern const device_t xtide_device;           /* xtide_xt */
extern const device_t xtide_at_device;        /* xtide_at */
extern const device_t xtide_acculogic_device; /* xtide_ps2 */
extern const device_t xtide_at_ps2_device;    /* xtide_at_ps2 */

/* Miscellaneous */
extern const device_t lba_enhancer_device;

extern void hdc_init(void);
extern void hdc_reset(void);

extern const char     *hdc_get_internal_name(int hdc);
extern int             hdc_get_from_internal_name(char *s);
extern int             hdc_has_config(int hdc);
extern const device_t *hdc_get_device(int hdc);
extern int             hdc_get_flags(int hdc);
extern int             hdc_available(int hdc);

#endif /*EMU_HDC_H*/
