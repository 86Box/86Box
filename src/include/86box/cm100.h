#ifndef EMU_CM100_H
#define EMU_CM100_H

#include <stddef.h>
#include <stdint.h>

/* The five signals are logical levels. */
typedef enum cm100_signal {
    CM100_RESPONSE,
    CM100_COMMAND,
    CM100_DATA,
    CM100_DATA_CLOCK,
    CM100_ATTENTION
} cm100_signal_t;

typedef enum cm100_motion {
    CM100_STOPPED,
    CM100_SPINNING_UP,
    CM100_READY,
    CM100_SEEKING,
    CM100_HOLD_TRACK,
    CM100_READING,
    CM100_SPINNING_DOWN
} cm100_motion_t;

typedef struct cm100_snapshot {
    uint64_t now_ns;
    uint64_t next_sector_ns;
    uint32_t sector;
    uint32_t sector_bit;
    uint32_t seek_sector;
    uint32_t medium_sectors;
    cm100_motion_t motion;
    float spindle_fraction;
    float pickup_fraction;
    uint8_t drive_error;
    uint8_t communication_error;
    uint8_t last_command[4];
    uint8_t attention;
    uint8_t door_open;
    uint8_t locked;
    uint8_t data_active;
} cm100_snapshot_t;

typedef struct cm100_drive cm100_drive_t;

typedef int (*cm100_read_sector_fn)(void *opaque, uint32_t lba,
                                    uint8_t sector[2352],
                                    uint8_t unreliable[2352]);
typedef void (*cm100_signal_fn)(void *opaque, cm100_signal_t signal,
                                int level, uint64_t time_ns);

cm100_drive_t *cm100_create(cm100_read_sector_fn read_sector,
                            cm100_signal_fn signal, void *opaque);
void cm100_destroy(cm100_drive_t *drive);
void cm100_reset(cm100_drive_t *drive, uint64_t now_ns);
void cm100_set_medium(cm100_drive_t *drive, uint32_t sectors, int present);
void cm100_advance(cm100_drive_t *drive, uint64_t now_ns);
void cm100_command_bit(cm100_drive_t *drive, int level, uint64_t now_ns);
int cm100_response_bit(cm100_drive_t *drive, uint64_t now_ns);
int cm100_data_edge(cm100_drive_t *drive, int rising, uint64_t now_ns);
void cm100_snapshot(const cm100_drive_t *drive, cm100_snapshot_t *out);

#endif
