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
 *          Copyright 2024-2025 starfrost
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>


nv_register_t ptimer_registers[] = {
    { NV3_PTIMER_INTR, "PTIMER - Interrupt Status", NULL, NULL},
    { NV3_PTIMER_INTR_EN, "PTIMER - Interrupt Enable", NULL, NULL,},
    { NV3_PTIMER_NUMERATOR, "PTIMER - Numerator", NULL, NULL, },
    { NV3_PTIMER_DENOMINATOR, "PTIMER - Denominator", NULL, NULL, },
    { NV3_PTIMER_TIME_0_NSEC, "PTIMER - Time0", NULL, NULL, },
    { NV3_PTIMER_TIME_1_NSEC, "PTIMER - Time1", NULL, NULL, },
    { NV3_PTIMER_ALARM_NSEC, "PTIMER - Alarm", NULL, NULL, },
    { NV_REG_LIST_END, NULL, NULL, NULL}, // sentinel value 
};

// ptimer init code
void nv3_ptimer_init()
{
    nv_log("Initialising PTIMER...");

    nv_log("Done!\n");    
}

// Handles the PTIMER alarm interrupt
void nv3_ptimer_interrupt(uint32_t num)
{
    nv3->ptimer.interrupt_status |= (1 << num);

    nv3_pmc_handle_interrupts(true);
}

// Ticks the timer.
void nv3_ptimer_tick(double real_time)
{
    // prevent a divide by zero
    if (nv3->ptimer.clock_numerator == 0
    || nv3->ptimer.clock_denominator == 0)
        return; 

    // get the current time

    // See Envytools. We need to use the frequency as a source. 
    // We need to figure out how many cycles actually occurred because this counts up every cycle...
    // However it seems that their formula is wrong. I can't be bothered to figure out what's going on and, based on documentation from NVIDIA,
    // timer_0 is meant to roll over every 4 seconds. Multiplying by 10 basically does the job.

    // Convert to microseconds
    double freq_base = (real_time / 1000000.0f) / ((double)1.0 / nv3->nvbase.memory_clock_frequency) * 10.0f;
    double current_time = freq_base * ((double)nv3->ptimer.clock_numerator) / (double)nv3->ptimer.clock_denominator; // *10.0?

    // truncate it 
    nv3->ptimer.time += (uint64_t)current_time;

    // Only log on ptimer alarm. Otherwise, it's too much spam.
    //nv_log("PTIMER time ticked (The value is now 0x%08x)\n", nv3->ptimer.time);

    // Check if the alarm has actually triggered...
    if (nv3->ptimer.time >= nv3->ptimer.alarm)
    {
        nv_log("PTIMER alarm interrupt fired because we reached TIME value 0x%08x\n", nv3->ptimer.alarm);
        nv3_ptimer_interrupt(NV3_PTIMER_INTR_ALARM);
    }
}

uint32_t nv3_ptimer_read(uint32_t address) 
{ 
    // always enabled

    nv_register_t* reg = nv_get_register(address, ptimer_registers, sizeof(ptimer_registers)/sizeof(ptimer_registers[0]));

    // Only log these when tehy actually tick
    if (address != NV3_PTIMER_TIME_0_NSEC
    && address != NV3_PTIMER_TIME_1_NSEC)
    {
        nv_log("PTIMER Read from 0x%08x", address);
    }

    uint32_t ret = 0x00;

    // if the register actually exists
    if (reg)
    {
        // on-read function
        if (reg->on_read)
            ret = reg->on_read();
        else
        {   
            // Interrupt state:
            // Bit 0: Alarm
            
            switch (reg->address)
            {
                case NV3_PTIMER_INTR:
                    ret = nv3->ptimer.interrupt_status;
                    break;
                case NV3_PTIMER_INTR_EN:
                    ret = nv3->ptimer.interrupt_enable;
                    break;
                case NV3_PTIMER_NUMERATOR:
                    ret = nv3->ptimer.clock_numerator; // 15:0
                    break;
                case NV3_PTIMER_DENOMINATOR:
                    ret = nv3->ptimer.clock_denominator ; //15:0
                    break;
                // 64-bit value
                // High part
                case NV3_PTIMER_TIME_0_NSEC:
                    ret = nv3->ptimer.time & 0xFFFFFFFF; //28:0
                    break;
                // Low part
                case NV3_PTIMER_TIME_1_NSEC:
                    ret = nv3->ptimer.time >> 32; // 31:5
                    break;
                case NV3_PTIMER_ALARM_NSEC: 
                    ret = nv3->ptimer.alarm; // 31:5
                    break;
            }

        }
        //TIME0 and TIME1 produce too much log spam that slows everything down 
        if (reg->address != NV3_PTIMER_TIME_0_NSEC
        && reg->address != NV3_PTIMER_TIME_1_NSEC)
        {
            if (reg->friendly_name)
            nv_log(": 0x%08x <- %s\n", ret, reg->friendly_name);
            else   
                nv_log("\n");
        }
    }
    else
    {
        nv_log(": Unknown register read (address=0x%08x), returning 0x00\n", address);
    }

    return ret;
}

void nv3_ptimer_write(uint32_t address, uint32_t value) 
{
    // before doing anything, check the subsystem enablement
    nv_register_t* reg = nv_get_register(address, ptimer_registers, sizeof(ptimer_registers)/sizeof(ptimer_registers[0]));

    nv_log("PTIMER Write 0x%08x -> 0x%08x", value, address);

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

                // Interrupt enablement state
                case NV3_PTIMER_INTR_EN:
                    nv3->ptimer.interrupt_enable = value & 0x1;
                    break;
                // nUMERATOR
                case NV3_PTIMER_NUMERATOR:
                    nv3->ptimer.clock_numerator = value & 0xFFFF; // 15:0
                    break;
                case NV3_PTIMER_DENOMINATOR:
                    // prevent Div0
                    if (!value)
                        value = 1;

                    nv3->ptimer.clock_denominator = value & 0xFFFF; //15:0
                    break;
                // 64-bit value
                // High part
                case NV3_PTIMER_TIME_0_NSEC:
                    nv3->ptimer.time |= (value) & 0xFFFFFFE0; //28:0
                    break;
                // Low part
                case NV3_PTIMER_TIME_1_NSEC:
                    nv3->ptimer.time |= ((uint64_t)(value & 0xFFFFFFE0) << 32); // 31:5
                    break;
                case NV3_PTIMER_ALARM_NSEC: 
                    nv3->ptimer.alarm = value & 0xFFFFFFE0; // 31:5
                    break;
            }
        }
    }
    else /* Completely unknown */
    {
        nv_log(": Unknown register write (address=0x%08x)\n", address);
    }
}