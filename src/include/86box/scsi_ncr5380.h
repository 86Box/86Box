/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the NCR 5380 chip made by NCR
 *          and used in various controllers.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          TheCollector1995, <mariogplayer@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2017-2018 Sarah Walker.
 *          Copyright 2017-2018 Fred N. van Kempen.
 *          Copyright 2017-2024 TheCollector1995.
 */

#ifndef SCSI_NCR5380_H
#define SCSI_NCR5380_H

#define NCR_CURDATA             0 /* current SCSI data (read only) */
#define NCR_OUTDATA             0 /* output data (write only) */
#define NCR_INITCOMMAND         1 /* initiator command (read/write) */
#define NCR_MODE                2 /* mode (read/write) */
#define NCR_TARGETCMD           3 /* target command (read/write) */
#define NCR_SELENABLE           4 /* select enable (write only) */
#define NCR_BUSSTATUS           4 /* bus status (read only) */
#define NCR_STARTDMA            5 /* start DMA send (write only) */
#define NCR_BUSANDSTAT          5 /* bus and status (read only) */
#define NCR_DMATARGET           6 /* DMA target (write only) */
#define NCR_INPUTDATA           6 /* input data (read only) */
#define NCR_DMAINIRECV          7 /* DMA initiator receive (write only) */
#define NCR_RESETPARITY         7 /* reset parity/interrupt (read only) */

#define ICR_DBP                 0x01
#define ICR_ATN                 0x02
#define ICR_SEL                 0x04
#define ICR_BSY                 0x08
#define ICR_ACK                 0x10
#define ICR_ARB_LOST            0x20
#define ICR_ARB_IN_PROGRESS     0x40

#define MODE_ARBITRATE          0x01
#define MODE_DMA                0x02
#define MODE_MONITOR_BUSY       0x04
#define MODE_ENA_EOP_INT        0x08

#define STATUS_ACK              0x01
#define STATUS_BUSY_ERROR       0x04
#define STATUS_PHASE_MATCH      0x08
#define STATUS_INT              0x10
#define STATUS_DRQ              0x40
#define STATUS_END_OF_DMA       0x80

#define TCR_IO                  0x01
#define TCR_CD                  0x02
#define TCR_MSG                 0x04
#define TCR_REQ                 0x08
#define TCR_LAST_BYTE_SENT      0x80


#define STATE_IDLE            0
#define STATE_COMMAND         1
#define STATE_DATAIN          2
#define STATE_DATAOUT         3
#define STATE_STATUS          4
#define STATE_MESSAGEIN       5
#define STATE_SELECT          6
#define STATE_MESSAGEOUT      7
#define STATE_MESSAGE_ID      8

#define DMA_IDLE              0
#define DMA_SEND              1
#define DMA_INITIATOR_RECEIVE 2

typedef struct ncr_t {
    uint8_t icr;
    uint8_t mode;
    uint8_t tcr;
    uint8_t data_wait;
    uint8_t isr;
    uint8_t output_data;
    uint8_t target_id;
    uint8_t tx_data;
    uint8_t msglun;
    uint8_t irq_state;

    uint8_t command[20];
    uint8_t msgout[4];
    uint8_t bus;

    int     msgout_pos;
    int     is_msgout;

    int     dma_mode;
    int     cur_bus;
    int     bus_in;
    int     new_phase;
    int     state;
    int     clear_req;
    int     wait_data;
    int     wait_complete;
    int     command_pos;
    int     data_pos;

    int     irq;

    double  period;

    void   *priv;
    void  (*dma_mode_ext)(void *priv, void *ext_priv);
    int   (*dma_send_ext)(void *priv, void *ext_priv);
    int   (*dma_initiator_receive_ext)(void *priv, void *ext_priv);
    void  (*timer)(void *ext_priv, double period);
} ncr_t;

extern int      ncr5380_cmd_len[8];

extern void     ncr5380_irq(ncr_t *ncr, int set_irq);
extern void	ncr5380_set_irq(ncr_t *ncr, int irq);
extern uint32_t ncr5380_get_bus_host(ncr_t *ncr);
extern void     ncr5380_bus_read(ncr_t *ncr);
extern void     ncr5380_bus_update(ncr_t *ncr, int bus);
extern void     ncr5380_write(uint16_t port, uint8_t val, ncr_t *ncr);
extern uint8_t  ncr5380_read(uint16_t port, ncr_t *ncr);

#ifdef EMU_DEVICE_H
extern const device_t scsi_lcs6821n_device;
extern const device_t scsi_pas_device;
extern const device_t scsi_rt1000b_device;
extern const device_t scsi_rt1000mc_device;
extern const device_t scsi_t128_device;
extern const device_t scsi_t228_device;
extern const device_t scsi_t130b_device;
extern const device_t scsi_ls2000_device;
#if defined(DEV_BRANCH) && defined(USE_SUMO)
extern const device_t scsi_scsiat_device;
#endif
#endif

#endif /*SCSI_NCR5380_H*/
