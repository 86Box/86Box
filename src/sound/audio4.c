/*
 * 86Box     A hypervisor and IBM PC system emulator that specializes in
 *           running old operating systems and software designed for IBM
 *           PC systems and compatibles from 1981 through fairly recent
 *           system designs based on the PCI bus.
 *
 *           This file is part of the 86Box distribution.
 *
 *           Interface to audio(4).
 *
 *
 *
 * Authors:  Nishi
 *
 *           Copyright 2025 Nishi.
 */
#include <stdint.h>

#include <86box/sound.h>

#define FREQ   SOUND_FREQ
#define BUFLEN SOUNDBUFLEN

static int midi_freq = 44100;
static int midi_buf_size = 4410;

void closeal(void){
}

void inital(void){
}

void givealbuffer(const void *buf){
}

void givealbuffer_music(const void *buf){
}

void givealbuffer_wt(const void *buf){
}

void givealbuffer_cd(const void *buf){
}

void givealbuffer_midi(const void *buf, const uint32_t size){
}
	
void al_set_midi(const int freq, const int buf_size){
    midi_freq     = freq;
    midi_buf_size = buf_size;
}
