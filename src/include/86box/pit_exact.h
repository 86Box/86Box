/*
 * Derived from Intel 8253/8254 documentation
 */
#ifndef EMU_PIT_EXACT_H
#define EMU_PIT_EXACT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum pitx_type_t {
    PITX_8253 = 0,
    PITX_8254 = 1
} pitx_type_t;

typedef enum pitx_state_t {
    PITX_WAIT_COUNT = 0,
    PITX_WAIT_TRIGGER,
    PITX_WAIT_GATE,
    PITX_LOAD_NEXT,
    PITX_COUNTING,
    PITX_RELOAD_NEXT,
    PITX_STROBE_RECOVER,
    PITX_DONE
} pitx_state_t;

typedef struct pitx_channel_t {
    pitx_type_t type;
    uint8_t control;
    uint8_t mode_raw;
    uint8_t mode;
    uint8_t rw_mode;
    bool bcd;

    uint8_t write_phase;
    uint8_t read_phase;
    bool count_latched;
    bool status_latched;
    uint16_t output_latch;
    uint8_t status_latch;

    uint16_t count_register;
    uint32_t counting_element; /* 0x10000 represents binary zero reload. */
    bool null_count;
    bool gate;
    bool output;
    bool armed;
    bool initial_load;
    bool incomplete_reload;
    bool ce_undefined;
    bool toggle_on_reload;
    pitx_state_t state;
    uint64_t clocks;
} pitx_channel_t;

typedef struct pitx_device_t {
    pitx_type_t type;
    pitx_channel_t channel[3];
    uint8_t last_control;
} pitx_device_t;

void pitx_init(pitx_device_t *pit, pitx_type_t type);
void pitx_reset(pitx_device_t *pit, pitx_type_t type);
void pitx_control_write(pitx_device_t *pit, uint8_t value);
void pitx_data_write(pitx_device_t *pit, unsigned channel, uint8_t value);
uint8_t pitx_data_read(pitx_device_t *pit, unsigned channel);
void pitx_set_gate(pitx_device_t *pit, unsigned channel, bool gate);
void pitx_tick(pitx_device_t *pit);
void pitx_tick_channel(pitx_device_t *pit, unsigned channel);
uint16_t pitx_get_count(const pitx_device_t *pit, unsigned channel);
bool pitx_get_output(const pitx_device_t *pit, unsigned channel);

#endif
