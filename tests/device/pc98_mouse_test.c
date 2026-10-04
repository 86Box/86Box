/* Exercise production PC-98 mouse/PPI/PIC and printer with host services mocked. */
#include "../../src/machine/m_pc98.c"
#include "../../src/pic.c"

static unsigned checks, failures;
#define CHECK(expr) do { checks++; if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; \
} } while (0)

uint64_t TIMER_USEC = 1ULL << 32, tsc, timer_target;
double cpuclock;
int mouse_type = MOUSE_TYPE_INTERNAL, mouse_input_mode, mouse_capture = 1;
int video_fullscreen, fullscreen_ui_visible;
static int host_x, host_y, host_buttons, coord_polls;
static unsigned irqs, irq_withdrawals;
int jumpered_internal_ecp_dma = -1;
static unsigned io_installs, printed;
static uint8_t printed_bytes[16];
static int printer_busy;
static const device_t host_printer_device = { .name = "Test Printer" };
void fatal(const char *fmt, ...) { fprintf(stderr, "%s\n", fmt); abort(); }

void timer_enable(pc_timer_t *timer) { timer->flags |= TIMER_ENABLED; }
void timer_disable(pc_timer_t *timer) { timer->flags &= ~TIMER_ENABLED; }
void timer_add(pc_timer_t *timer, void (*callback)(void *), void *priv, int start)
{ timer->callback = callback; timer->priv = priv; timer->flags = start ? TIMER_ENABLED : 0; }
void timer_stop(pc_timer_t *timer) { timer_disable(timer); }
int device_get_instance(void) { return 1; }
int machine_has_bus(int index, uint64_t bus) { (void)index; (void)bus; return 0; }
int machine;
void io_sethandler(uint16_t base, uint16_t size, uint8_t (*inb)(uint16_t, void *),
                   uint16_t (*inw)(uint16_t, void *), uint32_t (*inl)(uint16_t, void *),
                   void (*outb)(uint16_t, uint8_t, void *),
                   void (*outw)(uint16_t, uint16_t, void *),
                   void (*outl)(uint16_t, uint32_t, void *), void *priv)
{ (void)base; (void)size; (void)inb; (void)inw; (void)inl;
  (void)outb; (void)outw; (void)outl; (void)priv; io_installs++; }
void io_removehandler(uint16_t base, uint16_t size, uint8_t (*inb)(uint16_t, void *),
                      uint16_t (*inw)(uint16_t, void *), uint32_t (*inl)(uint16_t, void *),
                      void (*outb)(uint16_t, uint8_t, void *),
                      void (*outw)(uint16_t, uint16_t, void *),
                      void (*outl)(uint16_t, uint32_t, void *), void *priv)
{ (void)base; (void)size; (void)inb; (void)inw; (void)inl;
  (void)outb; (void)outw; (void)outl; (void)priv; }
void dma_set_drq(int ch, int state) { (void)ch; (void)state; }
int dma_channel_read(int ch) { (void)ch; return DMA_NODATA; }
int dma_channel_write(int ch, uint16_t value) { (void)ch; (void)value; return DMA_NODATA; }
const device_t *char_get_device(const int id) { (void)id; return &host_printer_device; }
static size_t host_printer_write(uint8_t *buf, size_t length, void *priv)
{ (void)priv; for (size_t i = 0; i < length; i++) printed_bytes[printed++] = buf[i]; return length; }
static uint32_t host_printer_status(void *priv)
{ (void)priv; return CHAR_LPT_ERROR | CHAR_LPT_SELECT | CHAR_LPT_ACK | (printer_busy ? CHAR_LPT_BUSY : 0); }
void *char_init(char_port_t *port, const device_t *device, int instance)
{
    (void)device; (void)instance;
    port->attached = 1;
    port->chardev.flags = CHAR_LPT_USESTROBE;
    port->chardev.write = host_printer_write;
    port->chardev.status = host_printer_status;
    port->chardev.priv = port;
    return port;
}
void char_update_status(char_port_t *port) { (void)port; }
void *device_add_inst(const device_t *device, int instance)
{ (void)device; (void)instance; return NULL; }
static void irq_notification(uint16_t mask, int set, void *priv)
{
    (void)priv;
    if (set && (mask & (1u << 13))) irqs++;
    if (!set && (mask & (1u << 13))) irq_withdrawals++;
}
static void test_update_pending(void) { }
int acpi_rtc_status;
void smi_raise(void) { }
int mouse_get_buttons_ex(void) { return host_buttons; }
void mouse_subtract_coords(int *x, int *y, int *ox, int *oy,
                          int min, int max, int invert, int abs)
{
    (void)ox; (void)oy; (void)min; (void)max; (void)invert; (void)abs;
    *x = host_x; *y = host_y; host_x = host_y = 0; coord_polls++;
}
void postcard_pc98_diag_write(uint8_t value) { (void)value; }

static uint8_t read_nibble(pc98_mb_t *d, uint8_t select)
{
    ppi_mouse_out(0x7fdd, select, d);
    return ppi_mouse_in(0x7fd9, d);
}

int main(void)
{
    /* Actual edge-PIC IRR logic, with scheduling alone replaced. Keep the
     * slave masked as during POST: a source may latch before BIOS unmasks it. */
    update_pending = test_update_pending;
    pic.icw3 = 0x80; /* PC-98 slave cascade on master IR7. */
    pic2.icw1 = 0x11;
    pic2.imr = 0xff;
    pic_set_irq_callback(irq_notification, NULL);
    pc98_mb_t board;
    memset(&board, 0, sizeof(board));
    board.dsw3 = 0x7c; /* VX power-on DIP setting: internal RAM connected. */
    pc98_mouse_reset(&board.mouse);
    ppi_ctx_mouse.mb = &board;
    ppi_ctx_mouse.which = 2;
    pc98_ppi8255_init(&board.ppi_mouse, &ppi_ctx_mouse,
                      pc98_ppi_read_cb, pc98_ppi_write_cb, NULL);
    CHECK(ppi_mouse_in(0x7fd9, &board) == 0xe0);
    CHECK(ppi_mouse_in(0x7fdb, &board) == 0x40);
    CHECK(ppi_mouse_in(0x7fdd, &board) == 0x08);
    /* BX4 SDIP's conventional-RAM setting must reach the legacy input pin.
     * Keep the high-speed clock pin low and the CPU mode on port C intact. */
    board.model = PC98_MODEL_BX4;
    board.sdip[0] = 0xff;
    board.sdip[2] = 0xdf; /* SW3-6 off. */
    pc98_sdip_translate(&board);
    CHECK(ppi_mouse_in(0x7fdb, &board) == 0x00);
    CHECK(ppi_mouse_in(0x7fdd, &board) == 0x0b);
    board.sdip[2] = 0xff; /* SW3-6 on. */
    pc98_sdip_translate(&board);
    CHECK(ppi_mouse_in(0x7fdb, &board) == 0x40);
    CHECK(ppi_mouse_in(0x7fdd, &board) == 0x0b);
    board.model = PC98_MODEL_VX;
    board.dsw1 = 0;
    board.dsw3 = 0x7c;
    /* SW1-5/6 and CPU mode pins must still be readable beside mouse control. */
    board.dsw1 = PC98_SW1_5 | PC98_SW1_6;
    CHECK(ppi_mouse_in(0x7fdd, &board) == 0x0b);
    board.dsw3 = 0x80;
    CHECK(ppi_mouse_in(0x7fdd, &board) == 0x0f);
    board.dsw3 = 0;

    ppi_mouse_out(0x7fdd, 0, &board); /* input direction cannot enable IRQ */
    pc98_mouse_tick(&board);
    CHECK(irqs == 0);
    ppi_mouse_out(0x7fdf, 0x93, &board); /* A/B input, C upper output */
    CHECK(ppi_mouse_in(0x7fdf, &board) == 0x93);

    host_x = 0x3c; host_y = -37;
    pc98_mouse_poll(&board);
    ppi_mouse_out(0x7fdd, 0x90, &board); /* latch & clear; IRQ inhibited */
    CHECK(board.mouse.x == 0 && board.mouse.y == 0);
    CHECK(read_nibble(&board, 0x90) == 0xec);
    CHECK(read_nibble(&board, 0xb0) == 0xe3);
    CHECK(read_nibble(&board, 0xd0) == 0xeb);
    CHECK(read_nibble(&board, 0xf0) == 0xed);
    CHECK(read_nibble(&board, 0xf0) == 0xed); /* reads don't destroy snapshot */
    host_x = 9; host_y = 7; host_buttons = 1;
    pc98_mouse_poll(&board);
    CHECK(read_nibble(&board, 0x90) == 0x6c); /* left button active low, old X */
    CHECK(board.mouse.x == 9 && board.mouse.y == 7); /* still accumulating */
    CHECK(read_nibble(&board, 0x10) == 0x69); /* live counter */
    ppi_mouse_out(0x7fdf, 0x0f, &board); /* BSR: PC7 rising edge */
    CHECK(ppi_mouse_in(0x7fd9, &board) == 0x69);
    CHECK(board.mouse.x == 0 && board.mouse.y == 0);
    ppi_mouse_out(0x7fdf, 0x0d, &board); /* BSR: PC6 select Y */
    CHECK(ppi_mouse_in(0x7fd9, &board) == 0x67);
    host_buttons = 2; pc98_mouse_poll(&board);
    CHECK(ppi_mouse_in(0x7fd9, &board) == 0xc7);
    host_buttons = 3; pc98_mouse_poll(&board);
    CHECK(ppi_mouse_in(0x7fd9, &board) == 0x47);
    host_buttons = 4; pc98_mouse_poll(&board); /* two-button guest */
    CHECK(ppi_mouse_in(0x7fd9, &board) == 0xe7);

    ppi_mouse_out(0x7fdf, 0x0e, &board); /* release hold */
    host_x = 250; pc98_mouse_poll(&board);
    host_x = 12; pc98_mouse_poll(&board);
    CHECK(board.mouse.x == 6); /* physical eight-bit wrap */

    /* A disabled mouse cannot cancel a peer card's pending IRQ13. Neither a
     * PPI write nor a disabled clock tick may clear the PIC's latched edge. */
    picint(1u << 13); /* Stand in for a SCSI card asserting the physical input. */
    CHECK(pic2.irr & 0x20);
    ppi_mouse_out(0x7fdf, 0x09, &board);
    CHECK(pic2.irr & 0x20);
    ppi_mouse_out(0x7fdd, 0x10, &board);
    CHECK(pic2.irr & 0x20);
    unsigned previous_irqs = irqs;
    pc98_mouse_tick(&board);
    CHECK(irqs == previous_irqs && (pic2.irr & 0x20));
    pic2.interrupt = 5;
    pic_acknowledge(&pic2); /* Only actual PIC acknowledgement consumes it. */
    CHECK(!(pic2.irr & 0x20));
    pic2.isr = 0;

    /* Stationary mouse must still generate interrupts. Inhibit stops future
     * pulses while preserving the current edge and any unrelated slave IRQ. */
    irqs = 0;
    pc98_mouse_tick(&board); CHECK(irqs == 0);
    ppi_mouse_out(0x7fdf, 0x08, &board); /* BSR: clear IRQ inhibit */
    pc98_mouse_tick(&board); pc98_mouse_tick(&board);
    CHECK(irqs == 2 && (pic2.irr & 0x20));
    picint(1u << 12);
    CHECK(pic2.irr == 0x30);
    unsigned previous_withdrawals = irq_withdrawals;
    pc98_mouse_tick(&board);
    CHECK(irqs == 3 && pic2.irr == 0x30);
    CHECK(irq_withdrawals == previous_withdrawals);
    ppi_mouse_out(0x7fdf, 0x09, &board);
    CHECK(pic2.irr == 0x30);
    pc98_mouse_tick(&board); CHECK(irqs == 3 && pic2.irr == 0x30);
    for (unsigned divider = 0; divider < 4; divider++) {
        tsc = 0;
        pc98_mouse_rate_out(0xbfdb, (uint8_t)(0xfc | divider), &board);
        uint64_t period = (board.mouse_timer.ts_integer << 32) | board.mouse_timer.ts_frac;
        CHECK(board.mouse.divider == divider);
        CHECK(period == (uint64_t)((1000000.0 / (120u >> divider)) * TIMER_USEC));
    }

    mouse_capture = 0; host_x = 4;
    int polls = coord_polls;
    pc98_mouse_tick(&board);
    CHECK(coord_polls == polls && host_x == 4);
    video_fullscreen = 1;
    pc98_mouse_tick(&board); CHECK(host_x == 0);
    mouse_type = MOUSE_TYPE_NONE; host_x = 5;
    pc98_mouse_tick(&board); CHECK(host_x == 5);

    pc98_mouse_reset(&board.mouse);
    CHECK(board.mouse.control == 0x10 && board.mouse.divider == 0);
    CHECK(board.mouse.x == 0 && board.mouse.latched_x == 0);
    CHECK(pc98_mouse_data(&board.mouse) == 0xe0);

    /* Native printer owns the PPI ports and uses the real common LPT/char
     * strobe pipeline. The host endpoint alone is mocked, just like a file. */
    lpt_ports[0].enabled = 1;
    lpt_ports[0].device = 1;
    lpt_set_next_inst(0);
    board.printer = lpt_port_native_device.init(&lpt_port_native_device);
    CHECK(board.printer != NULL && board.printer->addr == 0xffff);
    CHECK(io_installs == 0 && board.printer->irq == 0xff);
    lpt_devices_init();
    ppi_ctx_prn.mb = &board; ppi_ctx_prn.which = 1;
    pc98_ppi8255_init(&board.ppi_prn, &ppi_ctx_prn,
                      pc98_ppi_read_cb, pc98_ppi_write_cb, NULL);
    board.sys_port_c = PC98_SYS_C_PSTM;
    ppi_prn_out(0x40, 'X', &board); /* reset PPI directions are inputs */
    CHECK(printed == 0 && board.printer->dat == 0);
    ppi_prn_out(0x46, 0x82, &board); /* printer A/C output, B input */
    ppi_prn_out(0x46, 15, &board); /* PC7 high: idle */
    board.sys_port_c = 0;
    pc98_printer_control(&board);
    ppi_prn_out(0x40, 'A', &board); CHECK(printed == 0);
    ppi_prn_out(0x46, 14, &board); CHECK(printed == 0);
    ppi_prn_out(0x46, 15, &board);
    CHECK(printed == 1 && printed_bytes[0] == 'A');
    ppi_prn_out(0x46, 15, &board); CHECK(printed == 1);
    ppi_prn_out(0x40, 'B', &board);
    ppi_prn_out(0x44, 0x00, &board);
    ppi_prn_out(0x44, 0x80, &board);
    CHECK(printed == 2 && printed_bytes[1] == 'B');
    /* PSTBM must suppress the raw strobe even when the printer PPI toggles. */
    board.sys_port_c = PC98_SYS_C_PSTM; pc98_printer_control(&board);
    ppi_prn_out(0x40, 'C', &board);
    ppi_prn_out(0x46, 14, &board); ppi_prn_out(0x46, 15, &board);
    CHECK(printed == 2);
    board.sys_port_c = 0; pc98_printer_control(&board);
    printer_busy = 0;
    uint8_t identity = ppi_prn_in(0x42, &board);
    CHECK(identity & 4);
    printer_busy = 1;
    CHECK(ppi_prn_in(0x42, &board) == (identity & ~4u));
    lpt_port_native_device.close(board.printer);
    printf("PC98 mouse/printer: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
