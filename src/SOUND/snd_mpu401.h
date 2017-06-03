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
 * Version:	@(#)sound_mpu401.h	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		DOSBox Team,
 *		Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2008-2017 DOSBox Team.
 *		Copyright 2016-2017 Miran Grca.
 *		Copyright 2016-2017 TheCollector1995.
 */

#define MPU401_VERSION	0x15
#define MPU401_REVISION	0x01
#define MPU401_QUEUE 32
#define MPU401_TIMECONSTANT (60000000/1000.0f)
#define MPU401_RESETBUSY 27.0f

typedef enum MpuMode { M_UART,M_INTELLIGENT } MpuMode;
typedef enum MpuDataType {T_OVERFLOW,T_MARK,T_MIDI_SYS,T_MIDI_NORM,T_COMMAND} MpuDataType;

/* Messages sent to MPU-401 from host */
#define MSG_EOX	                        0xf7
#define MSG_OVERFLOW                    0xf8
#define MSG_MARK                        0xfc

/* Messages sent to host from MPU-401 */
#define MSG_MPU_OVERFLOW                0xf8
#define MSG_MPU_COMMAND_REQ             0xf9
#define MSG_MPU_END                     0xfc
#define MSG_MPU_CLOCK                   0xfd
#define MSG_MPU_ACK                     0xfe

typedef struct mpu_t
{
	int intelligent;
	MpuMode mode;
	int irq;
	uint8_t status;
	uint8_t queue[MPU401_QUEUE];
	int queue_pos,queue_used;
	struct track 
	{
		int counter;
		uint8_t value[8],sys_val;
		uint8_t vlength,length;
		MpuDataType type;
	} playbuf[8],condbuf;
	struct {
		int conductor,cond_req,cond_set, block_ack;
		int playing,reset;
		int wsd,wsm,wsd_start;
		int run_irq,irq_pending;
		int send_now;
		int eoi_scheduled;
		int data_onoff;
		uint32_t command_byte,cmd_pending;
		uint8_t tmask,cmask,amask;
		uint16_t midi_mask;
		uint16_t req_mask;
		uint8_t channel,old_chan;
	} state;
	struct {
		uint8_t timebase,old_timebase;
		uint8_t tempo,old_tempo;
		uint8_t tempo_rel,old_tempo_rel;
		uint8_t tempo_grad;
		uint8_t cth_rate,cth_counter;
		int clock_to_host,cth_active;
	} clock;
} mpu_t;

uint8_t MPU401_ReadData(mpu_t *mpu);

void mpu401_init(mpu_t *mpu, uint16_t addr, int irq, int mode);

extern int mpu401_standalone_enable;

void mpu401_device_add(void);
device_t mpu401_device;
