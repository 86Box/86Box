/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Handling of the SCSI controllers.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          TheCollector1995, <mariogplayer@gmail.com>
 *
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2017-2018 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/hdc.h>
#include <86box/hdd.h>
#include <86box/plat.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/rdisk.h>
#include <86box/scsi_disk.h>
#include <86box/scsi_aha154x.h>
#include <86box/scsi_buslogic.h>
#include <86box/scsi_ncr5380.h>
#include <86box/scsi_ncr53c8xx.h>
#include <86box/scsi_pcscsi.h>
#include <86box/scsi_qlogic.h>
#include <86box/scsi_spock.h>

int scsi_card_current[SCSI_CARD_MAX] = { 0, 0, 0, 0 };
double scsi_bus_speed[SCSI_BUS_MAX] = { 0.0, 0.0, 0.0, 0.0 };

static uint8_t next_scsi_bus = 0;

typedef const struct {
    const device_t *device;
} SCSI_CARD;

static SCSI_CARD scsi_cards[] = {
  // clang-format off
    { &device_none,              },
    /* ISA */
    { &scsi_lcs6821n_device,     },
    { &scsi_rt1000b_device,      },
    { &scsi_t128_device,         },
    { &scsi_t130b_device,        },
    /* ISA/Sidecar */
    { &scsi_ls2000_device,       },
    /* ISA16 */
    { &aha154xa_device,          },
    { &aha154xb_device,          },
    { &aha154xc_device,          },
    { &aha154xcf_device,         },
    { &aha154xcp_device,         },
    { &buslogic_542b_device,     },
    { &buslogic_542bh_device,    },
    { &buslogic_545s_device,     },
    { &buslogic_545c_device,     },
    /* MCA */
    { &aha1640_device,           },
    { &buslogic_640a_device,     },
    { &spock_device,             },
    { &tribble_device,           },
    { &ncr53c90a_mca_device,     },
    { &scsi_rt1000mc_device,     },
    { &scsi_t228_device,         },
    /* VLB */
    { &buslogic_445s_device,     },
    { &buslogic_445c_device,     },
    /* PCI */
    { &am53c974_pci_device,      },
    { &am53c974a_pci_device,     },
    { &buslogic_958d_pci_device, },
    { &ncr53c810_pci_device,     },
    { &ncr53c815_pci_device,     },
    { &ncr53c820_pci_device,     },
    { &ncr53c825a_pci_device,    },
    { &ncr53c860_pci_device,     },
    { &ncr53c875_pci_device,     },
    { &qla1040b_device,          },
    { &qla1080_device,           },
    { &qla1240_device,           },
    { &qla1280_device,           },
    { &qla12160a_device,         },
    { &dc390_pci_device,         },
    { NULL,                      },
  // clang-format on
};

void
scsi_reset(void)
{
    next_scsi_bus = 0;
}

uint8_t
scsi_get_bus(void)
{
    uint8_t ret = next_scsi_bus;

    if (next_scsi_bus >= SCSI_BUS_MAX)
        return 0xff;

    next_scsi_bus++;

    return ret;
}

int
scsi_card_available(int card)
{
    if (scsi_cards[card].device)
        return (device_available(scsi_cards[card].device));

    return 1;
}

const device_t *
scsi_card_getdevice(int card)
{
    return (scsi_cards[card].device);
}

int
scsi_card_has_config(int card)
{
    if (!scsi_cards[card].device)
        return 0;

    return (device_has_config(scsi_cards[card].device) ? 1 : 0);
}

const char *
scsi_card_get_internal_name(int card)
{
    return device_get_internal_name(scsi_cards[card].device);
}

int
scsi_card_get_from_internal_name(char *s)
{
    int c = 0;

    while (scsi_cards[c].device != NULL) {
        if (!strcmp(scsi_cards[c].device->internal_name, s))
            return c;
        c++;
    }

    return 0;
}

void
scsi_card_init(void)
{
    int max = SCSI_CARD_MAX;

    /* Do not initialize any controllers if we have do not have any SCSI
           bus left. */
    if (max > 0) {
        for (int i = 0; i < max; i++) {
            if ((scsi_card_current[i] > 0) && scsi_cards[scsi_card_current[i]].device)
                device_add_inst(scsi_cards[scsi_card_current[i]].device, i + 1);
        }
    }
}

void
scsi_bus_set_speed(uint8_t bus, double speed)
{
    scsi_bus_speed[bus] = speed;
}

double
scsi_bus_get_speed(uint8_t bus)
{
    return scsi_bus_speed[bus];
}
