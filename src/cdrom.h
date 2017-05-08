#ifndef __CDROM_H__
#define __CDROM_H__

/*CD-ROM stuff*/
#define CDROM_NUM	4

#define CDROM_PHASE_IDLE            0
#define CDROM_PHASE_COMMAND         1
#define CDROM_PHASE_COMPLETE        2
#define CDROM_PHASE_DATA_IN         3
#define CDROM_PHASE_DATA_IN_DMA     4
#define CDROM_PHASE_DATA_OUT        5
#define CDROM_PHASE_DATA_OUT_DMA    6
#define CDROM_PHASE_ERROR           0x80

#define BUF_SIZE 32768

#define CDROM_IMAGE 200

#define IDE_TIME (5 * 100 * (1 << TIMER_SHIFT))
#define CDROM_TIME (5 * 100 * (1 << TIMER_SHIFT))

typedef struct CDROM
{
	int (*ready)(uint8_t id);
	int (*medium_changed)(uint8_t id);
	int (*media_type_id)(uint8_t id);
	void (*audio_callback)(uint8_t id, int16_t *output, int len);
	void (*audio_stop)(uint8_t id);
	int (*readtoc)(uint8_t id, uint8_t *b, uint8_t starttrack, int msf, int maxlen, int single);
	int (*readtoc_session)(uint8_t id, uint8_t *b, int msf, int maxlen);
	int (*readtoc_raw)(uint8_t id, uint8_t *b, int msf, int maxlen);
	uint8_t (*getcurrentsubchannel)(uint8_t id, uint8_t *b, int msf);
	int (*pass_through)(uint8_t id, uint8_t *in_cdb, uint8_t *b, uint32_t *len);
	int (*readsector_raw)(uint8_t id, uint8_t *buffer, int sector, int ismsf, int cdrom_sector_type, int cdrom_sector_flags, int *len);
	void (*playaudio)(uint8_t id, uint32_t pos, uint32_t len, int ismsf);
	void (*load)(uint8_t id);
	void (*eject)(uint8_t id);
	void (*pause)(uint8_t id);
	void (*resume)(uint8_t id);
	uint32_t (*size)(uint8_t id);
	int (*status)(uint8_t id);
	int (*is_track_audio)(uint8_t id, uint32_t pos, int ismsf);
	void (*stop)(uint8_t id);
	void (*exit)(uint8_t id);
} CDROM;

#ifdef __MSC__
# pragma pack(push,1)
typedef struct
#else
typedef struct __attribute__((__packed__))
#endif
{
	uint8_t previous_command;
	int toctimes;

	int is_dma;

	int requested_blocks;		/* This will be set to something other than 1 when block reads are implemented. */

	int current_page_code;
	int current_page_len;

	int current_page_pos;

	int mode_select_phase;

	int total_length;
	int written_length;

	int do_page_save;

	uint8_t error;
	uint8_t features;
	uint16_t request_length;
	uint8_t status;
	uint8_t phase;

	uint32_t sector_pos;
	uint32_t sector_len;

	uint32_t packet_len;
	int packet_status;

	uint8_t atapi_cdb[16];
	uint8_t current_cdb[16];

	uint32_t pos;

	int callback;

	int data_pos;

	int cdb_len_setting;
	int cdb_len;

	int cd_status;
	int prev_status;

	int unit_attention;
	uint8_t sense[256];

	int request_pos;

	uint16_t buffer[390144];

	int times;

	uint32_t seek_pos;
	
	int total_read;

	int block_total;
	int all_blocks_total;
	
	int old_len;
	int block_descriptor_len;
	
	int init_length;
} cdrom_t;
#ifdef __MSC__
# pragma pack(pop)
#endif

extern cdrom_t cdrom[CDROM_NUM];

#ifdef __MSC__
# pragma pack(push,1)
typedef struct
#else
typedef struct __attribute__((__packed__))
#endif
{
	int enabled;

	int max_blocks_at_once;

	CDROM *handler;

	int host_drive;
	int prev_host_drive;

	uint8_t bus_type;		/* 0 = ATAPI, 1 = SCSI */
	uint8_t bus_mode;		/* Bit 0 = PIO suported;
					   Bit 1 = DMA supportd. */

	uint8_t ide_channel;

	uint8_t scsi_device_id;
	uint8_t scsi_device_lun;
	
	uint8_t sound_on;
	uint8_t atapi_dma;
} cdrom_drive_t;
#ifdef __MSC__
# pragma pack(pop)
#endif

extern cdrom_drive_t cdrom_drives[CDROM_NUM];

extern uint8_t atapi_cdrom_drives[8];

extern uint8_t scsi_cdrom_drives[16][8];

extern int (*ide_bus_master_read)(int channel, uint8_t *data, int transfer_length);
extern int (*ide_bus_master_write)(int channel, uint8_t *data, int transfer_length);
extern void (*ide_bus_master_set_irq)(int channel);

typedef struct
{
	int image_is_iso;

	uint32_t last_block;
	uint32_t cdrom_capacity;
	int image_inited;
	wchar_t image_path[1024];
	FILE* image;
	int image_changed;

	int cd_state;
	uint32_t cd_pos;
	uint32_t cd_end;
	int16_t cd_buffer[BUF_SIZE];
	int cd_buflen;
} cdrom_image_t;

cdrom_image_t cdrom_image[CDROM_NUM];

typedef struct
{
	uint32_t last_block;
	uint32_t cdrom_capacity;
	int ioctl_inited;
	char ioctl_path[8];
	int tocvalid;
	int cd_state;
	uint32_t cd_end;
	int16_t cd_buffer[BUF_SIZE];
	int cd_buflen;
	int actual_requested_blocks;
} cdrom_ioctl_t;

void ioctl_close(uint8_t id);

cdrom_ioctl_t cdrom_ioctl[CDROM_NUM];

uint32_t cdrom_mode_sense_get_channel(uint8_t id, int channel);
uint32_t cdrom_mode_sense_get_volume(uint8_t id, int channel);
void build_atapi_cdrom_map();
void build_scsi_cdrom_map();
int cdrom_CDROM_PHASE_to_scsi(uint8_t id);
int cdrom_atapi_phase_to_scsi(uint8_t id);
void cdrom_command(uint8_t id, uint8_t *cdb);
void cdrom_phase_callback(uint8_t id);
uint32_t cdrom_read(uint8_t channel, int length);
void cdrom_write(uint8_t channel, uint32_t val, int length);

#ifdef __cplusplus
extern "C" {
#endif

int cdrom_lba_to_msf_accurate(int lba);

#ifdef __cplusplus
}
#endif

void cdrom_reset(uint8_t id);
void cdrom_set_signature(int id);
void cdrom_request_sense_for_scsi(uint8_t id, uint8_t *buffer, uint8_t alloc_length);
void cdrom_update_cdb(uint8_t *cdb, int lba_pos, int number_of_blocks);
void cdrom_insert(uint8_t id);

#define cdrom_sense_error cdrom[id].sense[0]
#define cdrom_sense_key cdrom[id].sense[2]
#define cdrom_asc cdrom[id].sense[12]
#define cdrom_ascq cdrom[id].sense[13]
#define cdrom_drive cdrom_drives[id].host_drive

#endif