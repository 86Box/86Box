/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 PTIMER - PIT emulation
 *
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *
 *          Copyright 2024 starfrost
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86Box/86box.h>
#include <86Box/device.h>
#include <86Box/mem.h>
#include <86box/pci.h>
#include <86Box/rom.h> // DEPENDENT!!!
#include <86Box/video.h>
#include <86Box/nv/vid_nv.h>
#include <86Box/nv/vid_nv3.h>


nv_register_t ptimer_registers[] = {
    { NV3_PTIMER_INTR, "PTIMER - Interrupt Status", NULL, NULL},
    { NV3_PTIMER_INTR_EN, "PTIMER - Interrupt Enable", NULL, NULL,},
    { NV_REG_LIST_END, NULL, NULL, NULL}, // sentinel value 
};

// ptimer init code
void nv3_ptimer_init()
{
    nv_log("NV3: Initialising PTIMER...");

    nv_log("Done!\n");    
}

uint32_t nv3_ptimer_read(uint32_t address) 
{ 
    // before doing anything, check the subsystem enablement

    nv_register_t* reg = nv_get_register(address, ptimer_registers, sizeof(ptimer_registers)/sizeof(ptimer_registers[0]));

    // todo: friendly logging
    
    nv_log("NV3: PTIMER Read from 0x%08x", address);

    // if the register actually exists
    if (reg)
    {
        if (reg->friendly_name)
            nv_log(": %s\n", reg->friendly_name);
        else   
            nv_log("\n");

        // on-read function
        if (reg->on_read)
            return reg->on_read();
        else
        {   
            // Interrupt state:
            // Bit 0: Alarm
            
            switch (reg->address)
            {
                case NV3_PTIMER_INTR:
                    return nv3->ptimer.interrupt_status;
                case NV3_PTIMER_INTR_EN:
                    return nv3->ptimer.interrupt_enable;
            }
        }
    }

    return 0x00; 
}

void nv3_ptimer_write(uint32_t address, uint32_t value) 
{
    // before doing anything, check the subsystem enablement
    nv_register_t* reg = nv_get_register(address, ptimer_registers, sizeof(ptimer_registers)/sizeof(ptimer_registers[0]));

    nv_log("NV3: PTIMER Write 0x%08x -> 0x%08x\n", value, address);

    // if the register actually exists
    if (reg)
    {
        if (reg->friendly_name)
            nv_log(": %s\n", reg->friendly_name);
        else   
            nv_log("\n");

        // on-read function
        if (reg->on_write)
            reg->on_write(value);
        else
        {
            switch (reg->address)
            {
                // Interrupt state:
                // Bit 0 - Alarm

                case NV3_PTIMER_INTR:
                    nv3->ptimer.interrupt_status &= ~value;
                    nv3_pmc_clear_interrupts();
                    break;
                case NV3_PTIMER_INTR_EN:
                    nv3->ptimer.interrupt_enable = value & 0x00000001;
                    break;
            }
        }
    }
}