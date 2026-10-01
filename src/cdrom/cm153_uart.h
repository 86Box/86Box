#ifndef EMU_CM153_UART_H
#define EMU_CM153_UART_H

#include <stdint.h>
#include <86box/cm100.h>

#define CM153_UART_BIT_NS UINT64_C(52083)

typedef struct cm153_uart {
    uint8_t mode;
    uint8_t command;
    uint8_t status;
    uint8_t rx_data;
    uint8_t tx_data;
    uint8_t tx_phase;
    uint8_t rx_phase;
    uint8_t rx_parity;
    uint8_t rx_start;
    uint8_t await_mode;
    uint8_t break_active;
    uint8_t transmitting;
    uint8_t receiving;
} cm153_uart_t;

void cm153_uart_reset(cm153_uart_t *uart);
void cm153_uart_control(cm153_uart_t *uart, cm100_drive_t *drive,
                        uint8_t value, uint64_t now_ns);
int cm153_uart_transmit(cm153_uart_t *uart, uint8_t value);
int cm153_uart_tick(cm153_uart_t *uart, cm100_drive_t *drive,
                    uint64_t now_ns);
uint8_t cm153_uart_status(const cm153_uart_t *uart, int attention);
uint8_t cm153_uart_receive(cm153_uart_t *uart);

#endif
