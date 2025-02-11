/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the NEC uPD-765 and compatible floppy disk
 *          controller.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2018-2020 Fred N. van Kempen.
 */
#ifndef EMU_FDC_H
#define EMU_FDC_H

#define FDC_PRIMARY_ADDR        0x03f0
#define FDC_PRIMARY_IRQ         6
#define FDC_PRIMARY_DMA         2
#define FDC_PRIMARY_PCJR_ADDR   0x00f0
#define FDC_PRIMARY_PCJR_IRQ    6
#define FDC_PRIMARY_PCJR_DMA    2
#define FDC_SECONDARY_ADDR      0x0370
#define FDC_SECONDARY_IRQ       6
#define FDC_SECONDARY_DMA       2
#define FDC_TERTIARY_ADDR       0x0360
#define FDC_TERTIARY_IRQ        6
#define FDC_TERTIARY_DMA        2
#define FDC_QUATERNARY_ADDR     0x03e0
#define FDC_QUATERNARY_IRQ      6
#define FDC_QUATERNARY_DMA      2

#define FDC_FLAG_PCJR           0x01    /* PCjr */
#define FDC_FLAG_DISKCHG_ACTLOW 0x02    /* Amstrad, PS/1, PS/2 ISA */
#define FDC_FLAG_AT             0x04    /* AT+, PS/x */
#define FDC_FLAG_PS2            0x08    /* PS/1, PS/2 ISA */
#define FDC_FLAG_PS2_MCA        0x10    /* PS/2 MCA */
#define FDC_FLAG_SUPERIO        0x20    /* Super I/O chips */
#define FDC_FLAG_START_RWC_1    0x40    /* W83877F, W83977F */
#define FDC_FLAG_MORE_TRACKS    0x80    /* W83877F, W83977F, PC87306, PC87309 */
#define FDC_FLAG_NSC            0x100   /* PC87306, PC87309 */
#define FDC_FLAG_TOSHIBA        0x200   /* T1000, T1200 */
#define FDC_FLAG_AMSTRAD        0x400   /* Non-AT Amstrad machines */
#define FDC_FLAG_UMC            0x800   /* UMC UM8398 */
#define FDC_FLAG_ALI            0x1000  /* ALi M512x / M1543C */
#define FDC_FLAG_NO_DSR_RESET   0x2000  /* Has no DSR reset */
#define FDC_FLAG_DENSEL_INVERT  0x4000  /* Invert DENSEL polarity */
#define FDC_FLAG_FINTR          0x8000  /* Raise FINTR on data command finish */
#define FDC_FLAG_NEC            0x10000 /* Is NEC upd765-compatible */
#define FDC_FLAG_SEC            0x20000 /* Is Secondary */
#define FDC_FLAG_TER            0x40000 /* Is Tertiary */
#define FDC_FLAG_QUA            0x80000 /* Is Quaternary */

typedef struct fdc_t {
    uint8_t dor;
    uint8_t stat;
    uint8_t command;
    uint8_t processed_cmd;
    uint8_t dat;
    uint8_t st0;
    uint8_t swap;
    uint8_t dtl;

    uint8_t swwp;
    uint8_t disable_write;
    uint8_t st5;
    uint8_t st6;
    uint8_t error;
    uint8_t config;
    uint8_t pretrk;
    uint8_t power_down;

    uint8_t head;
    uint8_t lastdrive;
    uint8_t sector;
    uint8_t drive;
    uint8_t rate;
    uint8_t tc;
    uint8_t pnum;
    uint8_t ptot;

    uint8_t reset_stat;
    uint8_t seek_dir;
    uint8_t perp;
    uint8_t format_state;
    uint8_t format_n;
    uint8_t step;
    uint8_t noprec;
    uint8_t data_ready;

    uint8_t paramstogo;
    uint8_t enh_mode;
    uint8_t dma;
    uint8_t densel_polarity;
    uint8_t densel_force;
    uint8_t fifo;
    uint8_t tfifo;
    uint8_t fifobufpos;

    uint8_t drv2en;
    uint8_t gap;
    uint8_t enable_3f1;
    uint8_t format_sectors;
    uint8_t mfm;
    uint8_t deleted;
    uint8_t wrong_am;
    uint8_t sc;

    uint8_t fintr;
    uint8_t rw_drive;

    uint8_t lock;
    uint8_t dsr;

    uint8_t params[15];
    uint8_t specify[2];
    uint8_t res[11];
    uint16_t eot[4];
    uint8_t rwc[4];

    uint16_t pcn[4];

    uint16_t base_address;
    uint16_t rw_track;

    int bit_rate;    /* Should be 250 at start. */

    int bitcell_period;
    int boot_drive;

    int max_track;
    int satisfying_sectors;

    int flags;
    int interrupt;

    int irq;         /* Should be 6 by default. */
    int dma_ch;      /* Should be 2 by default. */

    int drvrate[4];

    void *fifo_p;

    sector_id_t read_track_sector;
    sector_id_t format_sector_id;

    uint64_t watchdog_count;

    pc_timer_t timer;
    pc_timer_t watchdog_timer;
} fdc_t;

extern void fdc_remove(fdc_t *fdc);
extern void fdc_poll(fdc_t *fdc);
extern void fdc_abort(fdc_t *fdc);
extern void fdc_set_dskchg_activelow(fdc_t *fdc);
extern void fdc_3f1_enable(fdc_t *fdc, int enable);
extern int  fdc_get_bit_rate(fdc_t *fdc);
extern int  fdc_get_bitcell_period(fdc_t *fdc);

/* A few functions to communicate between Super I/O chips and the FDC. */
extern void    fdc_update_enh_mode(fdc_t *fdc, int enh_mode);
extern int     fdc_get_rwc(fdc_t *fdc, int drive);
extern void    fdc_update_rwc(fdc_t *fdc, int drive, int rwc);
extern int     fdc_get_boot_drive(fdc_t *fdc);
extern void    fdc_update_boot_drive(fdc_t *fdc, int boot_drive);
extern void    fdc_update_densel_polarity(fdc_t *fdc, int densel_polarity);
extern uint8_t fdc_get_densel_polarity(fdc_t *fdc);
extern void    fdc_update_densel_force(fdc_t *fdc, int densel_force);
extern void    fdc_update_drvrate(fdc_t *fdc, int drive, int drvrate);
extern void    fdc_update_drv2en(fdc_t *fdc, int drv2en);

extern void fdc_noidam(fdc_t *fdc);
extern void fdc_nosector(fdc_t *fdc);
extern void fdc_nodataam(fdc_t *fdc);
extern void fdc_cannotformat(fdc_t *fdc);
extern void fdc_wrongcylinder(fdc_t *fdc);
extern void fdc_badcylinder(fdc_t *fdc);
extern void fdc_writeprotect(fdc_t *fdc);
extern void fdc_datacrcerror(fdc_t *fdc);
extern void fdc_headercrcerror(fdc_t *fdc);
extern void fdc_nosector(fdc_t *fdc);

extern int real_drive(fdc_t *fdc, int drive);

extern sector_id_t fdc_get_read_track_sector(fdc_t *fdc);
extern int         fdc_get_compare_condition(fdc_t *fdc);
extern int         fdc_is_deleted(fdc_t *fdc);
extern int         fdc_is_sk(fdc_t *fdc);
extern void        fdc_set_wrong_am(fdc_t *fdc);
extern void        fdc_set_power_down(fdc_t *fdc, uint8_t power_down);
extern int         fdc_get_drive(fdc_t *fdc);
extern int         fdc_get_perp(fdc_t *fdc);
extern int         fdc_get_format_n(fdc_t *fdc);
extern int         fdc_is_mfm(fdc_t *fdc);
extern double      fdc_get_hut(fdc_t *fdc);
extern double      fdc_get_hlt(fdc_t *fdc);
extern void        fdc_request_next_sector_id(fdc_t *fdc);
extern void        fdc_stop_id_request(fdc_t *fdc);
extern int         fdc_get_gap(fdc_t *fdc);
extern int         fdc_get_gap2(fdc_t *fdc, int drive);
extern int         fdc_get_dtl(fdc_t *fdc);
extern int         fdc_get_format_sectors(fdc_t *fdc);
extern uint8_t     fdc_get_swwp(fdc_t *fdc);
extern void        fdc_set_swwp(fdc_t *fdc, uint8_t swwp);
extern uint8_t     fdc_get_diswr(fdc_t *fdc);
extern void        fdc_set_diswr(fdc_t *fdc, uint8_t diswr);
extern uint8_t     fdc_get_swap(fdc_t *fdc);
extern void        fdc_set_swap(fdc_t *fdc, uint8_t swap);

extern void fdc_finishcompare(fdc_t *fdc, int satisfying);
extern void fdc_finishread(fdc_t *fdc);
extern void fdc_sector_finishcompare(fdc_t *fdc, int satisfying);
extern void fdc_sector_finishread(fdc_t *fdc);
extern void fdc_track_finishread(fdc_t *fdc, int condition);
extern int  fdc_is_verify(fdc_t *fdc);

extern void fdc_overrun(fdc_t *fdc);
extern void fdc_set_base(fdc_t *fdc, int base);
extern void fdc_set_irq(fdc_t *fdc, int irq);
extern void fdc_set_dma_ch(fdc_t *fdc, int dma_ch);
extern int  fdc_getdata(fdc_t *fdc, int last);
extern int  fdc_data(fdc_t *fdc, uint8_t data, int last);

extern void fdc_sectorid(fdc_t *fdc, uint8_t track, uint8_t side,
                         uint8_t sector, uint8_t size, uint8_t crc1,
                         uint8_t crc2);

extern uint8_t fdc_read(uint16_t addr, void *priv);
extern void    fdc_reset(void *priv);

extern uint8_t fdc_get_current_drive(void);

#ifdef EMU_DEVICE_H
extern const device_t fdc_xt_device;
extern const device_t fdc_xt_sec_device;
extern const device_t fdc_xt_ter_device;
extern const device_t fdc_xt_qua_device;
extern const device_t fdc_xt_t1x00_device;
extern const device_t fdc_xt_tandy_device;
extern const device_t fdc_xt_amstrad_device;
extern const device_t fdc_xt_umc_um8398_device;
extern const device_t fdc_pcjr_device;
extern const device_t fdc_at_device;
extern const device_t fdc_at_sec_device;
extern const device_t fdc_at_ter_device;
extern const device_t fdc_at_qua_device;
extern const device_t fdc_at_actlow_device;
extern const device_t fdc_at_smc_device;
extern const device_t fdc_at_ali_device;
extern const device_t fdc_at_winbond_device;
extern const device_t fdc_at_nsc_device;
extern const device_t fdc_at_nsc_dp8473_device;
extern const device_t fdc_ps2_device;
extern const device_t fdc_ps2_mca_device;
#endif

#endif /*EMU_FDC_H*/
