#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <86box/cm100.h>

#define UART_BIT_NS UINT64_C(52083)

static uint64_t now_ns;
static unsigned sector_reads;
static unsigned attention_changes;

static int
read_sector(void *opaque, uint32_t lba, uint8_t sector[2352],
            uint8_t unreliable[2352])
{
    (void) opaque;
    assert(lba == 0);
    memset(sector, 0, 2352);
    sector[0] = 0x96;
    sector[1] = 0x42;
    unreliable[1] = 1;
    sector_reads++;
    return 1;
}

static void
on_signal(void *opaque, cm100_signal_t signal, int level, uint64_t time_ns)
{
    (void) opaque;
    (void) level;
    (void) time_ns;
    if (signal == CM100_ATTENTION)
        attention_changes++;
}

static void
send_frame(cm100_drive_t *d, uint8_t value, int bad_parity)
{
    unsigned parity = 0;
    cm100_command_bit(d, 0, now_ns += UART_BIT_NS);
    for (unsigned i = 0; i < 8; i++) {
        unsigned bit = (value >> i) & 1;
        parity ^= bit;
        cm100_command_bit(d, bit, now_ns += UART_BIT_NS);
    }
    cm100_command_bit(d, (parity & 1) == !!bad_parity,
                      now_ns += UART_BIT_NS);
    cm100_command_bit(d, 1, now_ns += UART_BIT_NS);
}

static uint8_t
receive_frame(cm100_drive_t *d)
{
    uint8_t value = 0;
    unsigned parity = 0;
    assert(cm100_response_bit(d, now_ns += UART_BIT_NS) == 0);
    for (unsigned i = 0; i < 8; i++) {
        int bit = cm100_response_bit(d, now_ns += UART_BIT_NS);
        value |= bit << i;
        parity ^= bit;
    }
    parity ^= cm100_response_bit(d, now_ns += UART_BIT_NS);
    assert(parity == 1);
    assert(cm100_response_bit(d, now_ns += UART_BIT_NS) == 1);
    return value;
}

static void
command(cm100_drive_t *d, uint8_t value)
{
    send_frame(d, value, 0);
    assert(receive_frame(d) == value);
}

int
main(void)
{
    cm100_drive_t *d = cm100_create(read_sector, on_signal, NULL);
    assert(d);
    cm100_set_medium(d, 8, 1);
    now_ns = UINT64_C(20000000);
    cm100_advance(d, now_ns);
    cm100_snapshot_t s;
    cm100_snapshot(d, &s);
    assert(s.attention && s.drive_error == 0x0e);

    command(d, 0x4e);
    cm100_snapshot(d, &s);
    assert(!s.attention && !s.drive_error);

    command(d, 0x3a);
    for (unsigned i = 0; i < 12; i++) {
        send_frame(d, 0x9c, 0);
        uint8_t status = receive_frame(d);
        if (i == 5)
            assert(!(status & 0x08));
        if (i == 6)
            assert(status == 0);
    }

    command(d, 0x2d);
    for (unsigned i = 0; i < 12; i++) {
        send_frame(d, 0x9c, 0);
        uint8_t characteristic = receive_frame(d);
        if (i == 0)
            assert(characteristic == 0x01);
    }
    cm100_snapshot(d, &s);
    assert(!s.attention);

    /* The original driver seeks to 99:59:74 to discover the disc end.
       Status must report the last valid address even if the spindle stopped. */
    command(d, 0x59);
    command(d, 0x74);
    command(d, 0x59);
    command(d, 0x99);
    cm100_snapshot(d, &s);
    assert(!s.attention && s.motion == CM100_SPINNING_UP);
    now_ns += UINT64_C(2100000000);
    cm100_advance(d, now_ns);
    cm100_snapshot(d, &s);
    assert(s.attention && s.drive_error == 0x02);
    assert(s.motion == CM100_HOLD_TRACK && s.sector == 7);
    command(d, 0x3a);
    for (unsigned i = 0; i < 12; i++) {
        send_frame(d, 0x9c, 0);
        uint8_t status = receive_frame(d);
        if (i == 5)
            assert(status == 0x06);
        if (i == 6)
            assert(status == 0x02);
        if (i == 8)
            assert(status == 0x07);
        if (i == 9)
            assert(status == 0x02);
        if (i == 10)
            assert(status == 0x00);
    }
    command(d, 0x4e);
    cm100_snapshot(d, &s);
    assert(!s.attention && !s.drive_error);

    command(d, 0x17);
    command(d, 0x00);
    command(d, 0x02);
    command(d, 0x00);
    now_ns += UINT64_C(2100000000);
    cm100_advance(d, now_ns);
    cm100_snapshot(d, &s);
    assert(s.motion == CM100_READING);

    for (unsigned i = 0; i < 16; i++) {
        now_ns += 167;
        assert(cm100_data_edge(d, 0, now_ns) >= 0);
        now_ns += 167;
        assert(cm100_data_edge(d, 1, now_ns) ==
               ((i < 8 ? 0x96 : 0x42) >> (i & 7) & 1));
        cm100_snapshot(d, &s);
        if (i == 7)
            assert(s.attention);
        if (i == 15)
            assert(!s.attention);
    }
    assert(sector_reads == 1);
    assert(attention_changes >= 4);

    for (unsigned i = 0; i < 22; i++)
        cm100_command_bit(d, 0, now_ns += UART_BIT_NS);
    cm100_command_bit(d, 1, now_ns += UART_BIT_NS);
    cm100_snapshot(d, &s);
    assert(s.motion == CM100_STOPPED);
    assert(cm100_response_bit(d, now_ns += UART_BIT_NS) == 1);
    cm100_advance(d, now_ns += UINT64_C(20000000));
    cm100_snapshot(d, &s);
    assert(s.attention && s.drive_error == 0x0e);

    cm100_destroy(d);
    puts("CM-100 protocol and data edge tests passed");
    return 0;
}
