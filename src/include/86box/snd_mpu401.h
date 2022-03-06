/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Roland MPU-401 emulation.
 *
 *
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		DOSBox Team,
 *		Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2008-2020 DOSBox Team.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2016-2020 TheCollector1995.
 */

#ifndef SOUND_MPU401_H
#define SOUND_MPU401_H

#define MPU401_VERSION      0x15
#define MPU401_REVISION     0x01
#define MPU401_QUEUE        64
#define MPU401_INPUT_QUEUE  1024
#define MPU401_TIMECONSTANT (60000000 / 1000.0f)
#define MPU401_RESETBUSY    27.0f

/*helpers*/
#define M_GETKEY key[key / 32] & (1 << (key % 32))
#define M_SETKEY key[key / 32] |= (1 << (key % 32))
#define M_DELKEY key[key / 32] &= ~(1 << (key % 32))

typedef enum MpuMode {
    M_UART,
    M_INTELLIGENT
} MpuMode;

#define M_MCA 0x10

typedef enum MpuDataType {
    T_OVERFLOW,
    T_MARK,
    T_MIDI_SYS,
    T_MIDI_NORM,
    T_COMMAND
} MpuDataType;

typedef enum RecState {
    M_RECOFF,
    M_RECSTB,
    M_RECON
} RecState;

/* Messages sent to MPU-401 from host */
#define MSG_EOX      0xf7
#define MSG_OVERFLOW 0xf8
#define MSG_MARK     0xfc

/* Messages sent to host from MPU-401 */
#define MSG_MPU_OVERFLOW    0xf8
#define MSG_MPU_COMMAND_REQ 0xf9
#define MSG_MPU_END         0xfc
#define MSG_MPU_CLOCK       0xfd
#define MSG_MPU_ACK         0xfe

typedef struct mpu_t {
    uint16_t addr;
    int      uart_mode, intelligent,
        irq, midi_thru,
        queue_pos, queue_used;
    uint8_t rx_data, is_mca,
        status,
        queue[MPU401_QUEUE], pos_regs[8];
    MpuMode  mode;
    uint8_t  rec_queue[MPU401_INPUT_QUEUE];
    int      rec_queue_pos, rec_queue_used;
    uint32_t ch_toref[16];
    struct track {
        int     counter;
        uint8_t value[3], sys_val,
            vlength, length;
        MpuDataType type;
    } playbuf[8], condbuf;
    struct {
        int conductor, cond_req,
            cond_set, block_ack,
            playing, reset,
            wsd, wsm, wsd_start,
            run_irq, irq_pending,
            track_req,
            send_now, eoi_scheduled,
            data_onoff, clock_to_host,
            sync_in, sysex_in_finished,
            rec_copy;
        RecState rec;
        uint8_t  tmask, cmask,
            amask,
            last_rtcmd;
        uint16_t midi_mask, req_mask;
        uint32_t command_byte, cmd_pending,
            track, old_track;
    } state;
    struct {
        uint8_t timebase, old_timebase,
            tempo, old_tempo,
            tempo_rel, old_tempo_rel,
            tempo_grad, cth_rate[4],
            cth_mode, midimetro,
            metromeas;
        uint32_t cth_counter, cth_old,
            rec_counter;
        int32_t measure_counter, meas_old,
            freq;
        int   ticks_in, active;
        float freq_mod;
    } clock;
    struct {
        int all_thru, midi_thru,
            sysex_thru, commonmsgs_thru,
            modemsgs_in, commonmsgs_in,
            bender_in, sysex_in,
            allnotesoff_out, rt_affection,
            rt_out, rt_in,
            timing_in_stop, data_in_stop,
            rec_measure_end;
        uint8_t  prchg_buf[16];
        uint16_t prchg_mask;
    } filter;
    struct {
        int      on;
        uint8_t  chan, trmask;
        uint32_t key[4];
    } chanref[5], inputref[16];
    pc_timer_t mpu401_event_callback, mpu401_eoi_callback,
        mpu401_reset_callback;
    void (*ext_irq_update)(void *priv, int set);
    int (*ext_irq_pending)(void *priv);
    void *priv;
} mpu_t;

extern int mpu401_standalone_enable, mpu401_already_loaded;

extern const device_t mpu401_device;
extern const device_t mpu401_mca_device;

extern uint8_t MPU401_ReadData(mpu_t *mpu);
extern void    mpu401_write(uint16_t addr, uint8_t val, void *priv);
extern uint8_t mpu401_read(uint16_t addr, void *priv);
extern void    mpu401_setirq(mpu_t *mpu, int irq);
extern void    mpu401_change_addr(mpu_t *mpu, uint16_t addr);
extern void    mpu401_init(mpu_t *mpu, uint16_t addr, int irq, int mode, int receive_input);
extern void    mpu401_device_add(void);
extern void    mpu401_irq_attach(mpu_t *mpu, void (*ext_irq_update)(void *priv, int set), int (*ext_irq_pending)(void *priv), void *priv);

extern int  MPU401_InputSysex(void *p, uint8_t *buffer, uint32_t len, int abort);
extern void MPU401_InputMsg(void *p, uint8_t *msg, uint32_t len);

#endif /*SOUND_MPU401_H*/
