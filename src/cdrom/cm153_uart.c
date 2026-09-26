#include <string.h>
#include "cm153_uart.h"

enum {
    UART_TX_READY = 0x01,
    UART_RX_READY = 0x02,
    UART_TX_EMPTY = 0x04,
    UART_PARITY_ERROR = 0x08,
    UART_OVERRUN_ERROR = 0x10,
    UART_FRAME_ERROR = 0x20
};

void
cm153_uart_reset(cm153_uart_t *u)
{
    memset(u, 0, sizeof(*u));
    u->await_mode = 1;
    u->status = UART_TX_READY | UART_TX_EMPTY;
}

void
cm153_uart_control(cm153_uart_t *u, cm100_drive_t *drive,
                   uint8_t value, uint64_t now_ns)
{
    /* The original CM153.MSC writes six zeroes, 40h, 5Eh, then enables
       TX/RX. 40h is the 8251 internal reset. */
    if (u->await_mode) {
        u->mode = value;
        u->await_mode = 0;
        return;
    }
    if (value & 0x40) {
        cm153_uart_reset(u);
        return;
    }
    u->command = value;
    if (value & 0x10)
        u->status &= ~(UART_PARITY_ERROR | UART_OVERRUN_ERROR |
                       UART_FRAME_ERROR);
    if (value & 0x08) {
        u->break_active = 1;
    } else if (u->break_active) {
        u->break_active = 0;
        cm100_command_bit(drive, 1, now_ns);
    }
}

int
cm153_uart_transmit(cm153_uart_t *u, uint8_t value)
{
    if (u->await_mode || !(u->command & 0x01) ||
        !(u->status & UART_TX_READY))
        return 0;
    u->tx_data = value;
    u->tx_phase = 0;
    u->transmitting = 1;
    u->status &= ~(UART_TX_READY | UART_TX_EMPTY);
    return 1;
}

static int
cm153_tx_bit(const cm153_uart_t *u)
{
    if (!u->tx_phase)
        return 0;
    if (u->tx_phase <= 8)
        return (u->tx_data >> (u->tx_phase - 1)) & 1;
    if (u->tx_phase == 9) {
        unsigned parity = u->tx_data;
        parity ^= parity >> 4;
        parity ^= parity >> 2;
        parity ^= parity >> 1;
        return !(parity & 1);
    }
    return 1;
}

int
cm153_uart_tick(cm153_uart_t *u, cm100_drive_t *drive,
                 uint64_t now_ns)
{
    if (u->break_active) {
        cm100_command_bit(drive, 0, now_ns);
        return 1;
    }
    if (u->transmitting) {
        cm100_command_bit(drive, cm153_tx_bit(u), now_ns);
        if (++u->tx_phase == 11) {
            u->transmitting = 0;
            u->receiving = 1;
            u->rx_phase = u->rx_data = u->rx_parity = 0;
            u->status |= UART_TX_READY | UART_TX_EMPTY;
        }
        return 1;
    }
    if (u->receiving) {
        int bit = cm100_response_bit(drive, now_ns);
        if (!u->rx_phase) {
            u->rx_start = bit;
        } else if (u->rx_phase <= 8) {
            u->rx_data |= bit << (u->rx_phase - 1);
            u->rx_parity ^= bit;
        } else if (u->rx_phase == 9) {
            u->rx_parity ^= bit;
        } else {
            if (u->rx_start || !bit)
                u->status |= UART_FRAME_ERROR;
            if (u->rx_parity != 1)
                u->status |= UART_PARITY_ERROR;
            if (u->status & UART_RX_READY)
                u->status |= UART_OVERRUN_ERROR;
            u->status |= UART_RX_READY;
            u->receiving = 0;
            return 0;
        }
        u->rx_phase++;
        return 1;
    }
    return 0;
}

uint8_t
cm153_uart_status(const cm153_uart_t *u, int attention)
{
    /* The card routes ATTENTION to the 8251 active-low DSR input. CM153.MSC
       tests status bit 7 after each echoed command byte and treats a clear
       bit as an error. */
    return u->status | (attention ? 0 : 0x80);
}

uint8_t
cm153_uart_receive(cm153_uart_t *u)
{
    uint8_t value = u->rx_data;
    u->status &= ~UART_RX_READY;
    return value;
}
