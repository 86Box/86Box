#include "../../src/network/net_rtl8139.c"

uint64_t       tsc;
int            cpu_pci_speed;
netcard_conf_t net_cards_conf[NET_CARD_MAX];

static uint8_t guest_memory[65536];

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            exit(1);                                                        \
        }                                                                   \
    } while (0)

void
fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(1);
}

void
pci_irq(uint8_t slot, uint8_t pci_int, int level, int set, uint8_t *irq_state)
{
    *irq_state = set;
}

void
dma_bm_read(uint32_t address, uint8_t *buffer, uint32_t size, int transfer)
{
    CHECK(address + size <= sizeof(guest_memory));
    memcpy(buffer, guest_memory + address, size);
}

void
dma_bm_write(uint32_t address, const uint8_t *buffer, uint32_t size, int transfer)
{
    CHECK(address + size <= sizeof(guest_memory));
    memcpy(guest_memory + address, buffer, size);
}

static void
give_descriptor(unsigned index, bool end_of_ring)
{
    uint32_t desc[4] = { (1U << 31) | 2048, 0, 0x4000 + index * 2048, 0 };
    if (end_of_ring)
        desc[0] |= 1U << 30;
    memcpy(guest_memory + 0x1000 + index * 16, desc, sizeof(desc));
}

int
main(void)
{
    static RTL8139State s;
    uint8_t             frame[1514] = { 0 };

    s.clock_enabled = 1;
    s.bChipCmdState = CmdRxEnb;
    s.CpCmd         = CPlusRxEnb;
    s.RxConfig      = AcceptAllPhys;
    s.RxRingAddrLO  = 0x1000;
    give_descriptor(0, false);
    give_descriptor(1, true);

    CHECK(rtl8139_do_receive(&s, frame, sizeof(frame)) == sizeof(frame));
    CHECK(rtl8139_do_receive(&s, frame, sizeof(frame)) == sizeof(frame));

    /* With no free descriptor the frame is refused for retry, not dropped. */
    CHECK(rtl8139_do_receive(&s, frame, sizeof(frame)) == 0);
    CHECK(s.RxMissed == 0);
    CHECK(s.tally_counters.MissPkt == 0);
    CHECK(!(s.IntrStatus & RxOverflow));

    give_descriptor(0, false);
    CHECK(rtl8139_do_receive(&s, frame, sizeof(frame)) == sizeof(frame));
    CHECK(s.tally_counters.RxOk == 3);
    CHECK(s.currCPlusRxDesc == 1);

    puts("RTL8139 receive tests passed");
    return 0;
}
