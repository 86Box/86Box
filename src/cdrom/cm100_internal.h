#ifndef EMU_CM100_INTERNAL_H
#define EMU_CM100_INTERNAL_H

#include <86box/cm100.h>

enum {
    CM100_UART_BITS = 11,
    CM100_SECTOR_BYTES = 2352,
    CM100_STATUS_BYTES = 12,
    CM100_RESPONSE_QUEUE = 64
};

typedef enum cm100_error {
    CM100_ERROR_NONE = 0x00,
    CM100_ERROR_ADDRESS = 0x02,
    CM100_ERROR_SYNC = 0x03,
    CM100_ERROR_SEEK = 0x04,
    CM100_ERROR_TRACK = 0x05,
    CM100_ERROR_END = 0x06,
    CM100_ERROR_FOCUS = 0x08,
    CM100_ERROR_ILLEGAL_DISK = 0x09,
    CM100_ERROR_ROM = 0x0b,
    CM100_ERROR_RAM = 0x0c,
    CM100_ERROR_UNREADABLE = 0x0d,
    CM100_EVENT_RESET = 0x0e,
    CM100_EVENT_SPIN_DOWN = 0x0f,
    CM100_ERROR_DISK_ABSENT = 0x10,
    CM100_EVENT_UNIT_CHANGE = 0x13,
    CM100_ERROR_RADIAL = 0x14,
    CM100_EVENT_TIMEOUT = 0x15,
    CM100_EVENT_SPIN_UP = 0x16,
    CM100_ERROR_SUBCODE = 0x18,
    CM100_ERROR_RANGE = 0x19
} cm100_error_t;

typedef enum cm100_comm_error {
    CM100_COMM_NONE = 0,
    CM100_COMM_ILLEGAL = 0x01,
    CM100_COMM_PARITY = 0x07,
    CM100_COMM_GENERAL = 0x0a,
    CM100_COMM_PARAMETER = 0x11,
    CM100_COMM_ATTENTION = 0x12
} cm100_comm_error_t;

struct cm100_drive {
    cm100_read_sector_fn read_sector;
    cm100_signal_fn signal;
    void *opaque;
    uint64_t now_ns;
    uint64_t motion_start_ns;
    uint64_t motion_end_ns;
    uint64_t next_sector_ns;
    uint64_t reset_done_ns;
    uint32_t medium_sectors;
    uint32_t sector;
    uint32_t seek_origin;
    uint32_t seek_sector;
    uint32_t read_start;
    uint32_t read_end;
    uint32_t sector_bit;
    uint8_t sector_data[CM100_SECTOR_BYTES];
    uint8_t sector_urd[CM100_SECTOR_BYTES];
    uint8_t sector_loaded;
    uint8_t last_command[4];
    uint8_t status_address[3];
    uint8_t packet[7];
    uint8_t response_packet[CM100_STATUS_BYTES];
    uint8_t response_queue[CM100_RESPONSE_QUEUE];
    uint8_t packet_size;
    uint8_t packet_needed;
    uint8_t packet_blocked;
    uint8_t response_size;
    uint8_t response_pos;
    uint8_t queue_head;
    uint8_t queue_tail;
    uint8_t response_bit_pos;
    uint8_t rx_phase;
    uint8_t rx_byte;
    uint8_t rx_parity;
    uint8_t low_bits;
    uint8_t break_latched;
    uint8_t drive_error;
    uint8_t communication_error;
    uint8_t attention;
    uint8_t latched_attention;
    uint8_t data_attention;
    uint8_t door_open;
    uint8_t locked;
    uint8_t disc_present;
    uint8_t medium_known;
    uint8_t status_error;
    uint8_t reading_bounded;
    uint8_t pending_seek;
    uint8_t seek_read;
    uint8_t seek_invalid;
    uint8_t spin_notice;
    uint8_t output_data;
    uint8_t output_clock;
    uint8_t output_response;
    uint8_t input_command;
    cm100_motion_t motion;
};

void cm100_emit(cm100_drive_t *d, cm100_signal_t signal, int level);
void cm100_queue_response(cm100_drive_t *d, uint8_t value);
void cm100_protocol_byte(cm100_drive_t *d, uint8_t value, int parity_ok);
void cm100_protocol_reset(cm100_drive_t *d);
void cm100_raise_drive_error(cm100_drive_t *d, cm100_error_t error);
void cm100_raise_comm_error(cm100_drive_t *d, cm100_comm_error_t error);
void cm100_start_seek(cm100_drive_t *d, uint32_t sector, int read);
void cm100_start_spin(cm100_drive_t *d);
void cm100_stop_read(cm100_drive_t *d);
void cm100_pack_status(const cm100_drive_t *d, uint8_t packet[CM100_STATUS_BYTES]);
int cm100_decode_msf(const uint8_t msf[3], uint32_t *sector);
void cm100_encode_msf(uint32_t sector, uint8_t msf[3]);

#endif
