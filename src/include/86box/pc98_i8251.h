/* Intel 8251/8251A register-level USART core for PC-98. */
#ifndef EMU_86BOX_PC98_I8251_H
#define EMU_86BOX_PC98_I8251_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
    PC98_I8251_ST_TXRDY = 0x01,
    PC98_I8251_ST_RXRDY = 0x02,
    PC98_I8251_ST_TXE   = 0x04,
    PC98_I8251_ST_PE    = 0x08,
    PC98_I8251_ST_OE    = 0x10,
    PC98_I8251_ST_FE    = 0x20,
    PC98_I8251_ST_SYNDET= 0x40,
    PC98_I8251_ST_DSR   = 0x80
};

typedef void (*pc98_i8251_tx_cb)(void *opaque, uint8_t value);
typedef void (*pc98_i8251_line_cb)(void *opaque, int level);

typedef struct pc98_i8251_t {
    void *opaque;
    pc98_i8251_tx_cb tx_cb;
    pc98_i8251_line_cb rxrdy_cb;
    pc98_i8251_line_cb txrdy_cb;
    pc98_i8251_line_cb txempty_cb;

    uint8_t mode;
    uint8_t command;
    uint8_t status;
    uint8_t rx_data;
    uint8_t tx_data;
    uint8_t sync[2];
    uint8_t state;          /* 0 mode, 1 sync1, 2 sync2, 3 command */
    uint8_t sync_mode;
    uint8_t external_sync;
    uint8_t char_bits;
    uint8_t baud_factor;
    uint8_t stop_code;
    uint8_t parity_enable;
    uint8_t parity_even;
    uint8_t cts;
    uint8_t dsr;
} pc98_i8251_t;

void pc98_i8251_init(pc98_i8251_t *u, void *opaque, pc98_i8251_tx_cb tx_cb,
                     pc98_i8251_line_cb rxrdy_cb, pc98_i8251_line_cb txrdy_cb,
                     pc98_i8251_line_cb txempty_cb);
void pc98_i8251_reset(pc98_i8251_t *u);
uint8_t pc98_i8251_read(pc98_i8251_t *u, int control);
void pc98_i8251_write(pc98_i8251_t *u, int control, uint8_t value);
void pc98_i8251_receive(pc98_i8251_t *u, uint8_t value, int parity_error, int framing_error);
void pc98_i8251_set_cts(pc98_i8251_t *u, int asserted);
void pc98_i8251_set_dsr(pc98_i8251_t *u, int asserted);

#ifdef __cplusplus
}
#endif
#endif
