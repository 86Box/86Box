/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the IDE emulation for hard disks and ATAPI
 *		CD-ROM devices.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#ifndef EMU_IDE_H
#define EMU_IDE_H

#define IDE_BUS_MAX         4
#define IDE_CHAN_MAX        2

#define HDC_PRIMARY_BASE    0x01F0
#define HDC_PRIMARY_SIDE    0x03F6
#define HDC_PRIMARY_IRQ     14
#define HDC_SECONDARY_BASE  0x0170
#define HDC_SECONDARY_SIDE  0x0376
#define HDC_SECONDARY_IRQ   15
#define HDC_TERTIARY_BASE   0x0168
#define HDC_TERTIARY_SIDE   0x036E
#define HDC_TERTIARY_IRQ    10
#define HDC_QUATERNARY_BASE 0x01E8
#define HDC_QUATERNARY_SIDE 0x03EE
#define HDC_QUATERNARY_IRQ  11

enum {
    IDE_NONE = 0,
    IDE_HDD,
    IDE_ATAPI
};

#ifdef SCSI_DEVICE_H
typedef struct ide_s {
    uint8_t selected,
        atastat, error,
        command, fdisk;
    int type, board,
        irqstat, service,
        blocksize, blockcount,
        hdd_num, channel,
        pos, sector_pos,
        lba,
        reset, mdma_mode,
        do_initial_read;
    uint32_t secount, sector,
        cylinder, head,
        drive, cylprecomp,
        cfg_spt, cfg_hpc,
        lba_addr, tracks,
        spt, hpc;

    uint16_t *buffer;
    uint8_t  *sector_buffer;

    pc_timer_t timer;

    /* Stuff mostly used by ATAPI */
    scsi_common_t *sc;
    int            interrupt_drq;
    double         pending_delay;

    int (*get_max)(int ide_has_dma, int type);
    int (*get_timings)(int ide_has_dma, int type);
    void (*identify)(struct ide_s *ide, int ide_has_dma);
    void (*stop)(scsi_common_t *sc);
    void (*packet_command)(scsi_common_t *sc, uint8_t *cdb);
    void (*device_reset)(scsi_common_t *sc);
    uint8_t (*phase_data_out)(scsi_common_t *sc);
    void (*command_stop)(scsi_common_t *sc);
    void (*bus_master_error)(scsi_common_t *sc);
} ide_t;

extern ide_t *ide_drives[IDE_NUM];
#endif

/* Type:
        0 = PIO,
        1 = SDMA,
        2 = MDMA,
        3 = UDMA
   Return:
        -1 = Not supported,
        Anything else = maximum mode

   This will eventually be hookable. */
enum {
    TYPE_PIO = 0,
    TYPE_SDMA,
    TYPE_MDMA,
    TYPE_UDMA
};

/* Return:
        0 = Not supported,
        Anything else = timings

   This will eventually be hookable. */
enum {
    TIMINGS_DMA = 0,
    TIMINGS_PIO,
    TIMINGS_PIO_FC
};

extern int ide_ter_enabled, ide_qua_enabled;

#ifdef SCSI_DEVICE_H
extern ide_t *ide_get_drive(int ch);
extern void   ide_irq_raise(ide_t *ide);
extern void   ide_irq_lower(ide_t *ide);
extern void   ide_allocate_buffer(ide_t *dev);
extern void   ide_atapi_attach(ide_t *dev);
#endif

extern void *ide_xtide_init(void);
extern void  ide_xtide_close(void);

extern void     ide_writew(uint16_t addr, uint16_t val, void *priv);
extern void     ide_write_devctl(uint16_t addr, uint8_t val, void *priv);
extern void     ide_writeb(uint16_t addr, uint8_t val, void *priv);
extern uint8_t  ide_readb(uint16_t addr, void *priv);
extern uint8_t  ide_read_alt_status(uint16_t addr, void *priv);
extern uint16_t ide_readw(uint16_t addr, void *priv);

extern void ide_set_bus_master(int board,
                               int (*dma)(int channel, uint8_t *data, int transfer_length, int out, void *priv),
                               void (*set_irq)(int channel, void *priv), void *priv);

extern void win_cdrom_eject(uint8_t id);
extern void win_cdrom_reload(uint8_t id);

extern void ide_set_base(int board, uint16_t port);
extern void ide_set_side(int board, uint16_t port);

extern void ide_set_handlers(uint8_t board);
extern void ide_remove_handlers(uint8_t board);

extern void ide_pri_enable(void);
extern void ide_pri_disable(void);
extern void ide_sec_enable(void);
extern void ide_sec_disable(void);

extern void ide_board_set_force_ata3(int board, int force_ata3);
#ifdef EMU_ISAPNP_H
extern void ide_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv);
#endif

extern double ide_atapi_get_period(uint8_t channel);
#ifdef SCSI_DEVICE_H
extern void ide_set_callback(ide_t *ide, double callback);
#endif
extern void ide_set_board_callback(uint8_t board, double callback);

extern void ide_padstr(char *str, const char *src, int len);
extern void ide_padstr8(uint8_t *buf, int buf_size, const char *src);

extern int (*ide_bus_master_dma)(int channel, uint8_t *data, int transfer_length, int out, void *priv);
extern void (*ide_bus_master_set_irq)(int channel, void *priv);
extern void *ide_bus_master_priv[2];

extern uint8_t ide_read_ali_75(void);
extern uint8_t ide_read_ali_76(void);

#endif /*EMU_IDE_H*/
