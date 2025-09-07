/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Fast, high-frequency, guest CPU-independent timer for Riva emulation.
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *
 *          Copyright 2024-2025 starfrost
 */

/*
RivaTimer

This is a fast, high-frequency, guest CPU-independent timer.

The main 86box timer is dependent on the TSC (time-stamp counter) register of the emulated CPU core.
This is fine for most purposes and has advantages in the fields of synchronisation and integrates neatly with 
the clock dividers of the PC architecture, but in the case of the RIVA 128 it does not particularly suffice 
(although it can be made to work with various techniques) since the clock source on the RIVA 128 is on the board itself
and the GPU has several different clocks that control different parts of the GPU (e.g., PTIMER runs on the memory clock but the core gpu is using the pixel clock).

As faster graphics cards that offload more and more of the 3D graphics pipeline are emulated in the future, more and more work needs to be done by the emulator and 
issues of synchronisation with a host CPU will simply make that work harder. Some features that are required for 

Architecture    Brand Name      3D Features
NV1 (1995)      NV1             Some weird URBS rectangle crap but feature set generally similar to nv3 but a bit worse
NV3 (1997)      RIVA 128 (ZX)   Triangle setup, edge-slope calculations, edge interpolation, span-slope calculations, span interpolation (Color-buffer, z-buffer, texture mapping, filtering)
NV4 (1998)      RIVA TNT        NV3 + 2x1 pixel pipelines + 32-bit colour + larger textures + trilinear + more ram (16mb)
NV5 (1999)      RIVA TNT2       NV4 + higher clock speed
NV10 (1999)     GeForce 256     NV5 + initial geometry transformation + lighting (8x lights) + MPEG-2 motion compensation + 4x1 pixel pipelines
NV15 (2000)     GeForce 2       NV10 + First attempt at programmability + 4x2 pixel pipelines
NV20 (2001)     GeForce 3       Programmable shaders!

As you can see, the performance basically exponentially increases over a period of only 4 years. 

So I decided to create this timer that is completely separate from the CPU Core.
*/

#pragma once
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86box/86box.h>

#ifdef _WIN32     
#include <Windows.h>
// Linux & MacOS should have the same API since OSX 10.12 
#else
#include <time.h>
#endif

typedef struct rivatimer_s
{
    struct rivatimer_s*     prev;           // Previous Rivatimer
    double                  period;         // Period in uS before firing
    double                  value;          // The current value of the rivatimer
    bool                    running;        // Is this RivaTimer running?
    struct rivatimer_s*     next;           // Next RivaTimer
    void                    (*callback)(double real_time);  // Callback to call on fire
    #ifdef _WIN32
    LARGE_INTEGER           starting_time;  // Starting time.
    #else
    struct timespec         starting_time;  // Starting time.
    #endif
    double                  time;           // Accumulated time in uS.
} rivatimer_t;

void rivatimer_init(void);                                              // Initialise the Rivatimer.
rivatimer_t* rivatimer_create(double period, void (*callback)(double real_time));
void rivatimer_destroy(rivatimer_t* rivatimer_ptr);

void rivatimer_update_all(void);
void rivatimer_start(rivatimer_t* rivatimer_ptr);
void rivatimer_stop(rivatimer_t* rivatimer_ptr);
double rivatimer_get_time(rivatimer_t* rivatimer_ptr);
void rivatimer_set_callback(rivatimer_t* rivatimer_ptr, void (*callback)(double real_time));
void rivatimer_set_period(rivatimer_t* rivatimer_ptr, double period);
