/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the IDE emulation for hard disks and ATAPI
 *          CD-ROM devices.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 */
#ifndef EMU_IDE_H
#define EMU_IDE_H

#define IDE_NUM             10    /* 8 drives per AT IDE + 2 for XT IDE */
#define ATAPI_NUM           10    /* 8 drives per AT IDE + 2 for XT IDE */

#define IDE_BUS_MAX         4
#define IDE_CHAN_MAX        2

#define HDC_PRIMARY_BASE    0x01f0
#define HDC_PRIMARY_SIDE    0x03f6
#define HDC_PRIMARY_IRQ     14
#define HDC_SECONDARY_BASE  0x0170
#define HDC_SECONDARY_SIDE  0x0376
#define HDC_SECONDARY_IRQ   15
#define HDC_TERTIARY_BASE   0x01e8
#define HDC_TERTIARY_SIDE   0x03ee
#define HDC_TERTIARY_IRQ    11
#define HDC_QUATERNARY_BASE 0x0168
#define HDC_QUATERNARY_SIDE 0x036e
#define HDC_QUATERNARY_IRQ  10

enum {
    IDE_NONE = 0,       /* Absent master or both. */
    IDE_HDD,            /* Hard disk. */
    IDE_ATAPI,          /* ATAPI device. */
    IDE_RESERVED,       /* Reserved, do not use. */
    IDE_SHADOW,         /* Shadow flag, do not assign on is own. */
    IDE_HDD_SHADOW,     /* Shadow of a hard disk. */
    IDE_ATAPI_SHADOW    /* Shadow of an ATAPI device. */
};

typedef struct ide_tf_s {
    union {
        uint8_t cylprecomp;
        uint8_t features;
    };
    union {
        uint8_t secount;
        uint8_t phase;
    };
    union {
        uint16_t cylinder;
        uint16_t request_length;
    };
    union {
        uint8_t atastat;
        uint8_t status;
    };
    uint8_t error;
    uint8_t sector;
    union {
        uint8_t drvsel;
        struct {
            uint8_t head :4;
            uint8_t pad  :2; 
            uint8_t lba  :1; 
            uint8_t pad0 :1;
        };
    };
    uint32_t pos;
} ide_tf_t;

#ifdef _TIMER_H_
typedef struct ide_s {
    /* The rest. */
    uint8_t  selected;
    uint8_t  command;
    uint8_t  head;
    uint8_t  params_specified;
    int      type;
    int      board;
    int      irqstat;
    int      service;
    int      blocksize;
    int      blockcount;
    int      hdd_num;
    int      channel;
    int      sector_pos;
    int      reset;
    int      mdma_mode;
    int      do_initial_read;
    uint32_t drive;
    uint32_t cfg_spt;
    uint32_t cfg_hpc;
    uint32_t lba_addr;
    uint32_t tracks;
    uint32_t spt;
    uint32_t hpc;

    uint16_t *buffer;
    uint8_t  *sector_buffer;

    pc_timer_t timer;

    /* Task file. */
    ide_tf_t *     tf;

    /* Stuff mostly used by ATAPI */
#ifdef SCSI_DEVICE_H
    scsi_common_t *sc;
#else
    void *         sc;
#endif
    int            interrupt_drq;
    double         pending_delay;

#ifdef SCSI_DEVICE_H
    int     (*get_max)(const struct ide_s *ide, const int ide_has_dma, const int type);
    int     (*get_timings)(const struct ide_s *ide, const int ide_has_dma, const int type);
    void    (*identify)(const struct ide_s *ide, const int ide_has_dma);
    void    (*stop)(const scsi_common_t *sc);
    void    (*packet_command)(scsi_common_t *sc, const uint8_t *cdb);
    void    (*device_reset)(scsi_common_t *sc);
    uint8_t (*phase_data_out)(scsi_common_t *sc);
    void    (*command_stop)(scsi_common_t *sc);
    void    (*bus_master_error)(scsi_common_t *sc);
#else
    void *  get_max;
    void *  get_timings;
    void *  identify;
    void *  stop;
    void *  device_reset;
    void *  phase_data_out;
    void *  command_stop;
    void *  bus_master_error;
#endif
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
    TYPE_PIO  = 0,
    TYPE_SDMA = 1,
    TYPE_MDMA = 2,
    TYPE_UDMA = 3
};

/* Return:
        0 = Not supported,
        Anything else = timings

   This will eventually be hookable. */
enum {
    TIMINGS_DMA    = 0,
    TIMINGS_PIO    = 1,
    TIMINGS_PIO_FC = 2
};

extern int ide_ter_enabled;
extern int ide_qua_enabled;

#ifdef SCSI_DEVICE_H
extern ide_t *ide_get_drive(int ch);
extern void   ide_irq(ide_t *ide, int set, int log);
extern void   ide_allocate_buffer(ide_t *dev);
extern void   ide_atapi_attach(ide_t *dev);
#endif

extern void *ide_xtide_init(void);
extern void  ide_xtide_close(void);

extern void  ide_drives_set_shadow(void);

extern void     ide_writew(uint16_t addr, uint16_t val, void *priv);
extern void     ide_write_devctl(uint16_t addr, uint8_t val, void *priv);
extern void     ide_writeb(uint16_t addr, uint8_t val, void *priv);
extern uint8_t  ide_readb(uint16_t addr, void *priv);
extern uint8_t  ide_read_alt_status(uint16_t addr, void *priv);
extern uint16_t ide_readw(uint16_t addr, void *priv);

extern void ide_set_bus_master(int board,
                               int (*dma)(uint8_t *data, int transfer_length, int out, void *priv),
                               void (*set_irq)(uint8_t status, void *priv), void *priv);

extern void win_cdrom_eject(uint8_t id);
extern void win_cdrom_reload(uint8_t id);

extern void ide_set_base_addr(int board, int base, uint16_t port);
extern void ide_set_irq(int board, int irq);

extern void ide_handlers(uint8_t board, int set);

extern void ide_board_set_force_ata3(int board, int force_ata3);
#ifdef EMU_ISAPNP_H
extern void ide_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv);
extern void ide_pnp_config_changed_1addr(uint8_t ld, isapnp_device_config_t *config, void *priv);
#endif

extern double ide_atapi_get_period(uint8_t channel);
#ifdef SCSI_DEVICE_H
extern void ide_set_callback(ide_t *ide, double callback);
#endif
extern void ide_set_board_callback(uint8_t board, double callback);

extern void ide_padstr(char *str, const char *src, int len);
extern void ide_padstr8(uint8_t *buf, int buf_size, const char *src);

extern uint8_t ide_read_ali_75(void);
extern uint8_t ide_read_ali_76(void);

/* Legacy #define's. */
#define ide_irq_raise(ide) ide_irq(ide, 1, 1)
#define ide_irq_lower(ide) ide_irq(ide, 0, 1)

#define ide_set_base(board, port) ide_set_base_addr(board, 0, port)
#define ide_set_side(board, port) ide_set_base_addr(board, 1, port)

#define ide_pri_enable()     ide_handlers(0, 1)
#define ide_pri_disable()    ide_handlers(0, 0)
#define ide_sec_enable()     ide_handlers(1, 1)
#define ide_sec_disable()    ide_handlers(1, 0)

#define ide_set_handlers(board) ide_handlers(board, 1)
#define ide_remove_handlers(board) ide_handlers(board, 0)

#endif /*EMU_IDE_H*/
