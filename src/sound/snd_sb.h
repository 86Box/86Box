/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Sound Blaster emulation.
 *
 * Version:	@(#)sound_sb.h	1.0.3	2018/03/18
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#ifndef SOUND_SND_SB_H
# define SOUND_SND_SB_H


#define SADLIB		1	/* No DSP */
#define SB1		2	/* DSP v1.05 */
#define SB15		3	/* DSP v2.00 */
#define SB2		4	/* DSP v2.01 - needed for high-speed DMA */
#define SBPRO		5	/* DSP v3.00 */
#define SBPRO2		6	/* DSP v3.02 + OPL3 */
#define SB16		7	/* DSP v4.05 + OPL3 */
#define SADGOLD		8	/* AdLib Gold */
#define SND_WSS		9	/* Windows Sound System */
#define SND_PAS16	10	/* Pro Audio Spectrum 16 */


extern const device_t sb_1_device;
extern const device_t sb_15_device;
extern const device_t sb_mcv_device;
extern const device_t sb_2_device;
extern const device_t sb_pro_v1_device;
extern const device_t sb_pro_v2_device;
extern const device_t sb_pro_mcv_device;
extern const device_t sb_16_device;
extern const device_t sb_awe32_device;


#endif	/*SOUND_SND_SB_H*/
