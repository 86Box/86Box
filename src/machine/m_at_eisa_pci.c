/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Machines with both PCI and EISA.
 *
 *          The combination was never common and did not last: EISA was
 *          the server bus of the early nineties and PCI replaced it from
 *          underneath, so the boards that carried both were built in the
 *          few years when a buyer might already own EISA cards worth
 *          keeping. The AIR 54TDP is one of them -- a 430HX Socket 7
 *          board, dual capable, with four EISA slots, four PCI slots, no
 *          ISA at all, and an Adaptec AIC-7880 soldered on.
 *
 *          What makes it unusual to emulate is that it has no PIIX. The
 *          430HX's normal partner is the 82371SB, but this board pairs
 *          the 82439HX with Intel's PCI-EISA bridge instead, and so the
 *          ESC is the south bridge outright: the interrupt controllers,
 *          the DMA controllers, the timers and the PCI interrupt routing
 *          are all in it.
 *
 * Authors: Michael Pratte, <mpratte@makefox.group>
 *
 *          Copyright 2026 Michael Pratte.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/flash.h>
#include <86box/pci.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/sio.h>
#include <86box/chipset.h>
#include <86box/eisa.h>
#include <86box/esc.h>
#include <86box/scsi_aic7xxx.h>
#include <86box/machine.h>
#include <86box/rom.h>
#include <86box/keyboard.h>

static const device_config_t at_54tdp_config[] = {
    // clang-format off
    /* Whether a machine that has never had the configuration utility run
       on it starts with an empty but well formed EISA store, the way one
       off a factory line does, or with nothing at all, the way one whose
       battery has been out does. Either way what the utility writes is
       kept: this decides the starting point and nothing else. */
    {
        .name           = "auto_eisa_config",
        .description    = "Initialize EISA configuration store",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t at_54tdp_device = {
    .name          = "AIR 54TDP",
    .internal_name = "54tdp_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = at_54tdp_config
};

int
machine_at_54tdp_init(const machine_t *model)
{
    int ret;

    /* Two builds of the same BIOS exist: 1.03 for one processor and 1.51
       for two. This machine is the uniprocessor one, so it gets 1.03;
       nothing here would know what to do with the second socket. */
    ret = bios_load_linear("roms/machines/54tdp/54tdp103.rom",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    /* The bridge sits where a south bridge would, because on this board
       it is one. */
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    /* The on-board SCSI, then the four slots. */
    /* The BIOS only ever routes PIRQC and PIRQD, and it programs them
       the other way round from what this table used to assume: PIRQC to
       IRQ 11 and PIRQD to IRQ 10 (the ESC's route registers in a POST
       trace read 60h..63h = 80 80 0B 0A). The on-board SCSI is on IRQ 10
       on the real board -- lspci shows 00:08.0 at irq 10 -- and the BIOS
       tells the operating system so, so its INTA is PIRQD. With it on
       PIRQC the chip raised IRQ 11 while Windows 2000 had its handler on
       IRQ 10: every command on the on-board bus completed unnoticed and
       was reset after the driver's timeout, eight seconds at a time. */
    /* The rest is what the BIOS itself reports through its PCI IRQ
       routing table (INT 1Ah, B10Eh, as PCIREG prints it): the on-board
       SCSI at 08h with INTA alone, and four slots at 09h to 0Ch -- slot 4
       down to slot 1 -- each one a rotation of the last. There is no
       device 0Dh on this board: a card put there had no routing at all. */
    pci_register_slot(0x08, PCI_CARD_SCSI,   4, 0, 0, 0); /* Onboard */
    pci_register_slot(0x09, PCI_CARD_NORMAL, 2, 3, 4, 1); /* Slot 4 */
    pci_register_slot(0x0a, PCI_CARD_NORMAL, 3, 4, 1, 2); /* Slot 3 */
    pci_register_slot(0x0b, PCI_CARD_NORMAL, 4, 1, 2, 3); /* Slot 2 */
    pci_register_slot(0x0c, PCI_CARD_NORMAL, 1, 2, 3, 4); /* Slot 1 */

    /* Four EISA slots. */
    eisa_init(4);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&i430hx_device);
    device_add(&pceb_device);
    device_add(&esc_device);
    device_add(&fdc37c669_device);

    /* The board identifier the BIOS reads out of the ESC. */
    esc_set_board_id("AIR", 0x0901, 0);
    /* The Adaptec soldered to the board, as the firmware records it. */
    esc_set_embedded_id("ADP", 0x7880, 0);

    /* The Adaptec is not optional on this board: it is soldered to it,
       and the system BIOS carries its option ROM. */
    device_add(machine_get_scsi_device(machine));

    /* This board takes two processors and its firmware says so, but the
       APIC in the ESC has nowhere to deliver a message: there is no local
       APIC here. An operating system that believes the table and routes
       its interrupts through the APIC therefore loses them -- the mouse
       first, because nothing else needs IRQ 12. Blanking the table is what
       the other dual-capable Socket 7 boards do. The firmware is AMI, whose
       last POST code before booting is 00, not Award's FF. */
    device_add(&ioapic_ami_device);

    /* The firmware lives in a Winbond W29C011A, and some of what setup
       stores goes back into it rather than into CMOS. Without a flash part
       here those writes land on read-only memory and are lost. */
    device_add(&winbond_flash_w29c011a_device);

    return ret;
}

static const device_config_t d823_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "d823_112",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "PhoenixBIOS 4.0 - Revision 1.12.823",
                .internal_name = "d823_112",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 131072,
                .files         = { "roms/machines/d823/d823_112.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t d823_device = {
    .name          = "Siemens-Nixdorf D823",
    .internal_name = "d823_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = d823_config
};

/* The Siemens-Nixdorf D823: a 430NX board with the PCEB and ESC, two
   Socket 5s, four EISA slots, three PCI and one ISA, and a PC Technology
   RZ1000 for its IDE. Everything below is what the technical manual
   (A26361-D823-Z120-1-7619) and the v1.12 firmware say:

   - The firmware reaches PCI configuration space by mechanism #2 (ports
     C000h up) and names three on-board functions there: the PCMC at 0,
     the PCEB at 1 and the IDE chip at 2, whose table (F000:24F9) sets its
     BARs to the compatibility addresses, 1F0h, 3F4h, 170h and 374h, and its
     timing registers at 40h-4Fh -- the RZ1000's layout.
   - It runs empty initialisation tables against devices 0Dh, 0Eh and 0Fh,
     the three slots, with INTA, INTB and INTC the primary pin of slots 1,
     2 and 3 (manual, PCI Device Configuration).
   - It writes the ESC's EISA identifier itself (table at F000:2445,
     50h-53h = 4D C9 EE 11, "SNI" EE1 revision 1), and points general
     purpose chip select 0 at 0C90h, where the board's switch block S500
     answers: bit 0 of 0C91h is switch 1, recovery mode, and an open
     switch reads as one. */
int
machine_at_d823_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000e0000, 131072, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_2);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x02, PCI_CARD_IDE,         0, 0, 0, 0); /* Onboard RZ1000 */
    /* The BIOS's routing table (F000:346E) and the SNI configuration file's
       slot list put PCI slot 1 at device 0Fh and slot 3 at device 0Dh. */
    pci_register_slot(0x0f, PCI_CARD_NORMAL,      1, 2, 3, 4); /* Slot 1 */
    pci_register_slot(0x0e, PCI_CARD_NORMAL,      2, 3, 4, 1); /* Slot 2 */
    pci_register_slot(0x0d, PCI_CARD_NORMAL,      3, 4, 1, 2); /* Slot 3 */

    /* Four EISA slots, and the one ISA slot beside them. */
    eisa_init(4);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&i430nx_device);
    device_add(&pceb_device);
    device_add(&esc_device);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(machine_get_ide_device(machine));

    /* What the firmware writes into the ESC's identifier registers anyway,
       so that anything reading the slots before it has run sees the board. */
    esc_set_board_id("SNI", 0xee11, 0);

    /* 128 KB of flash with a boot block at the top: switch 1 of S500 runs
       a "second, non-erasable rudimentary BIOS" from it. The part is the
       28F001BX-T, the first of the devices Siemens's FLASHBIO knows. It is
       also where the EISA configuration lives: the firmware keeps it in the
       second 4 KB parameter block, at FFFFD000h, erasing and programming it
       itself with the BIOS write enable in ESC register 43h bit 3. It never
       touches the ESC's configuration RAM (no access to 0C00h), so this
       board has no EISA configuration store option; the CMOS is the plain
       128-byte AT one, with the EISA status in byte 33h. */
    device_add(&intel_flash_bxt_device);

    return ret;
}
