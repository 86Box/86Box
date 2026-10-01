#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <86box/cm100.h>
#include "cm153_uart.h"

static uint64_t now_ns;

int
main(void)
{
    cm100_drive_t *drive = cm100_create(NULL, NULL, NULL);
    cm153_uart_t uart;
    cm153_uart_reset(&uart);
    cm100_advance(drive, now_ns = UINT64_C(20000000));
    cm153_uart_control(&uart, drive, 0x00, now_ns);
    cm153_uart_control(&uart, drive, 0x40, now_ns);
    cm153_uart_control(&uart, drive, 0x5e, now_ns);
    cm153_uart_control(&uart, drive, 0x27, now_ns);

    assert(cm153_uart_status(&uart, 1) == 0x05);
    assert(cm153_uart_transmit(&uart, 0x4e));
    assert(!(cm153_uart_status(&uart, 1) & 0x01));
    for (unsigned i = 0; i < 22; i++)
        cm153_uart_tick(&uart, drive, now_ns += CM153_UART_BIT_NS);
    assert(cm153_uart_status(&uart, 0) == 0x87);
    assert(cm153_uart_receive(&uart) == 0x4e);
    assert(cm153_uart_status(&uart, 0) == 0x85);

    cm153_uart_control(&uart, drive, 0x0a, now_ns);
    for (unsigned i = 0; i < 22; i++)
        cm153_uart_tick(&uart, drive, now_ns += CM153_UART_BIT_NS);
    cm153_uart_control(&uart, drive, 0x27, now_ns += CM153_UART_BIT_NS);
    cm100_snapshot_t state;
    cm100_snapshot(drive, &state);
    assert(state.motion == CM100_STOPPED);
    cm100_advance(drive, now_ns += UINT64_C(20000000));
    cm100_snapshot(drive, &state);
    assert(state.attention && state.drive_error == 0x0e);

    cm100_destroy(drive);
    puts("CM-153 USART test passed");
    return 0;
}
