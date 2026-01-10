/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Main video-rendering module.
 *
 *          Video timings are set individually by the graphics cards.
 * 
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 */
#include <stdatomic.h>
#define PNG_DEBUG 0
#include <png.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/config.h>
#include <86box/timer.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_svga.h>

#include <minitrace/minitrace.h>

volatile int screenshots = 0;
uint8_t      edatlookup[4][4];
uint8_t      egaremap2bpp[256];
uint8_t      fontdat[2048][8];            /* IBM CGA font */
uint8_t      fontdatm[2048][16];          /* IBM MDA font */
uint8_t      fontdatw[512][32];           /* Wyse700 font */
uint8_t      fontdat8x12[256][16];        /* MDSI Genius font */
uint8_t      fontdat12x18[256][36];       /* IM1024 font */
dbcs_font_t *fontdatksc5601       = NULL; /* Korean KSC-5601 font */
dbcs_font_t *fontdatksc5601_user  = NULL; /* Korean KSC-5601 user defined font */
int          herc_blend           = 0;
int          frames               = 0;
int          fullchange           = 0;
int          video_grayscale      = 0;
int          video_graytype       = 0;
int          monitor_index_global = 0;
uint32_t    *video_6to8           = NULL;
uint32_t    *video_8togs          = NULL;
uint32_t    *video_8to32          = NULL;
uint32_t    *video_15to32         = NULL;
uint32_t    *video_16to32         = NULL;
monitor_t          monitors[MONITORS_NUM];
monitor_settings_t monitor_settings[MONITORS_NUM];
atomic_bool        doresize_monitors[MONITORS_NUM];

#ifdef _WIN32
void * (*__cdecl video_copy)(void *_Dst, const void *_Src, size_t _Size) = memcpy;
#else
void *(*video_copy)(void *__restrict, const void *__restrict, size_t);
#endif

PALETTE cgapal = {
    {0,0,0},    {0,42,0},   {42,0,0},   {42,21,0},
    {0,0,0},    {0,42,42},  {42,0,42},  {42,42,42},
    {0,0,0},    {21,63,21}, {63,21,21},  {63,63,21},
    {0,0,0},    {21,63,63}, {63,21,63}, {63,63,63},

    {0,0,0},    {0,0,42},   {0,42,0},   {0,42,42},
    {42,0,0},   {42,0,42},  {42,21,00}, {42,42,42},
    {21,21,21}, {21,21,63}, {21,63,21}, {21,63,63},
    {63,21,21}, {63,21,63}, {63,63,21}, {63,63,63},

    {0,0,0},    {0,21,0},   {0,0,42},   {0,42,42},
    {42,0,21},  {21,10,21}, {42,0,42},  {42,0,63},
    {21,21,21}, {21,63,21}, {42,21,42}, {21,63,63},
    {63,0,0},   {42,42,0},  {63,21,42}, {41,41,41},

    {0,0,0},   {0,42,42},   {42,0,0},   {42,42,42},
    {0,0,0},   {0,42,42},   {42,0,0},   {42,42,42},
    {0,0,0},   {0,63,63},   {63,0,0},   {63,63,63},
    {0,0,0},   {0,63,63},   {63,0,0},   {63,63,63},
};
PALETTE cgapal_mono[6] = {
    { /* 0 - green, 4-color-optimized contrast. */
        {0x00,0x00,0x00},{0x00,0x0d,0x03},{0x01,0x17,0x05},
        {0x01,0x1a,0x06},{0x02,0x28,0x09},{0x02,0x2c,0x0a},
        {0x03,0x39,0x0d},{0x03,0x3c,0x0e},{0x00,0x07,0x01},
        {0x01,0x13,0x04},{0x01,0x1f,0x07},{0x01,0x23,0x08},
        {0x02,0x31,0x0b},{0x02,0x35,0x0c},{0x05,0x3f,0x11},{0x0d,0x3f,0x17},
    },
    { /* 1 - green, 16-color-optimized contrast. */
        {0x00,0x00,0x00},{0x00,0x0d,0x03},{0x01,0x15,0x05},
        {0x01,0x17,0x05},{0x01,0x21,0x08},{0x01,0x24,0x08},
        {0x02,0x2e,0x0b},{0x02,0x31,0x0b},{0x01,0x22,0x08},
        {0x02,0x28,0x09},{0x02,0x30,0x0b},{0x02,0x32,0x0c},
        {0x03,0x39,0x0d},{0x03,0x3b,0x0e},{0x09,0x3f,0x14},{0x0d,0x3f,0x17},
    },
    { /* 2 - amber, 4-color-optimized contrast. */
        {0x00,0x00,0x00},{0x15,0x05,0x00},{0x20,0x0b,0x00},
        {0x24,0x0d,0x00},{0x33,0x18,0x00},{0x37,0x1b,0x00},
        {0x3f,0x26,0x01},{0x3f,0x2b,0x06},{0x0b,0x02,0x00},
        {0x1b,0x08,0x00},{0x29,0x11,0x00},{0x2e,0x14,0x00},
        {0x3b,0x1e,0x00},{0x3e,0x21,0x00},{0x3f,0x32,0x0a},{0x3f,0x38,0x0d},
    },
    { /* 3 - amber, 16-color-optimized contrast. */
        {0x00,0x00,0x00},{0x15,0x05,0x00},{0x1e,0x09,0x00},
        {0x21,0x0b,0x00},{0x2b,0x12,0x00},{0x2f,0x15,0x00},
        {0x38,0x1c,0x00},{0x3b,0x1e,0x00},{0x2c,0x13,0x00},
        {0x32,0x17,0x00},{0x3a,0x1e,0x00},{0x3c,0x1f,0x00},
        {0x3f,0x27,0x01},{0x3f,0x2a,0x04},{0x3f,0x36,0x0c},{0x3f,0x38,0x0d},
    },
    { /* 4 - grey, 4-color-optimized contrast. */
        {0x00,0x00,0x00},{0x0e,0x0f,0x10},{0x15,0x17,0x18},
        {0x18,0x1a,0x1b},{0x24,0x25,0x25},{0x27,0x28,0x28},
        {0x33,0x34,0x32},{0x37,0x38,0x35},{0x09,0x0a,0x0b},
        {0x11,0x12,0x13},{0x1c,0x1e,0x1e},{0x20,0x22,0x22},
        {0x2c,0x2d,0x2c},{0x2f,0x30,0x2f},{0x3c,0x3c,0x38},{0x3f,0x3f,0x3b},
    },
    { /* 5 - grey, 16-color-optimized contrast. */
        {0x00,0x00,0x00},{0x0e,0x0f,0x10},{0x13,0x14,0x15},
        {0x15,0x17,0x18},{0x1e,0x20,0x20},{0x20,0x22,0x22},
        {0x29,0x2a,0x2a},{0x2c,0x2d,0x2c},{0x1f,0x21,0x21},
        {0x23,0x25,0x25},{0x2b,0x2c,0x2b},{0x2d,0x2e,0x2d},
        {0x34,0x35,0x33},{0x37,0x37,0x34},{0x3e,0x3e,0x3a},{0x3f,0x3f,0x3b},
    }
};

const uint32_t shade[5][256] = {
    {0}, // RGB Color (unused)
    {0}, // RGB Grayscale (unused)
    {    // Amber monitor
        0xff000000, 0xff060000, 0xff090000, 0xff0d0000, 0xff100000, 0xff120100, 0xff150100, 0xff170100, 0xff1a0100, 0xff1c0100, 0xff1e0200, 0xff210200, 0xff230200, 0xff250300, 0xff270300, 0xff290300,
        0xff2b0400, 0xff2d0400, 0xff2f0400, 0xff300500, 0xff320500, 0xff340500, 0xff360600, 0xff380600, 0xff390700, 0xff3b0700, 0xff3d0700, 0xff3f0800, 0xff400800, 0xff420900, 0xff440900, 0xff450a00,
        0xff470a00, 0xff480b00, 0xff4a0b00, 0xff4c0c00, 0xff4d0c00, 0xff4f0d00, 0xff500d00, 0xff520e00, 0xff530e00, 0xff550f00, 0xff560f00, 0xff581000, 0xff591000, 0xff5b1100, 0xff5c1200, 0xff5e1200,
        0xff5f1300, 0xff601300, 0xff621400, 0xff631500, 0xff651500, 0xff661600, 0xff671600, 0xff691700, 0xff6a1800, 0xff6c1800, 0xff6d1900, 0xff6e1a00, 0xff701a00, 0xff711b00, 0xff721c00, 0xff741c00,
        0xff751d00, 0xff761e00, 0xff781e00, 0xff791f00, 0xff7a2000, 0xff7c2000, 0xff7d2100, 0xff7e2200, 0xff7f2300, 0xff812300, 0xff822400, 0xff832500, 0xff842600, 0xff862600, 0xff872700, 0xff882800,
        0xff8a2900, 0xff8b2900, 0xff8c2a00, 0xff8d2b00, 0xff8e2c00, 0xff902c00, 0xff912d00, 0xff922e00, 0xff932f00, 0xff953000, 0xff963000, 0xff973100, 0xff983200, 0xff993300, 0xff9b3400, 0xff9c3400,
        0xff9d3500, 0xff9e3600, 0xff9f3700, 0xffa03800, 0xffa23900, 0xffa33a00, 0xffa43a00, 0xffa53b00, 0xffa63c00, 0xffa73d00, 0xffa93e00, 0xffaa3f00, 0xffab4000, 0xffac4000, 0xffad4100, 0xffae4200,
        0xffaf4300, 0xffb14400, 0xffb24500, 0xffb34600, 0xffb44700, 0xffb54800, 0xffb64900, 0xffb74a00, 0xffb94a00, 0xffba4b00, 0xffbb4c00, 0xffbc4d00, 0xffbd4e00, 0xffbe4f00, 0xffbf5000, 0xffc05100,
        0xffc15200, 0xffc25300, 0xffc45400, 0xffc55500, 0xffc65600, 0xffc75700, 0xffc85800, 0xffc95900, 0xffca5a00, 0xffcb5b00, 0xffcc5c00, 0xffcd5d00, 0xffce5e00, 0xffcf5f00, 0xffd06000, 0xffd26101,
        0xffd36201, 0xffd46301, 0xffd56401, 0xffd66501, 0xffd76601, 0xffd86701, 0xffd96801, 0xffda6901, 0xffdb6a01, 0xffdc6b01, 0xffdd6c01, 0xffde6d01, 0xffdf6e01, 0xffe06f01, 0xffe17001, 0xffe27201,
        0xffe37301, 0xffe47401, 0xffe57501, 0xffe67602, 0xffe77702, 0xffe87802, 0xffe97902, 0xffeb7a02, 0xffec7b02, 0xffed7c02, 0xffee7e02, 0xffef7f02, 0xfff08002, 0xfff18103, 0xfff28203, 0xfff38303,
        0xfff48403, 0xfff58503, 0xfff68703, 0xfff78803, 0xfff88903, 0xfff98a04, 0xfffa8b04, 0xfffb8c04, 0xfffc8d04, 0xfffd8f04, 0xfffe9005, 0xffff9105, 0xffff9205, 0xffff9305, 0xffff9405, 0xffff9606,
        0xffff9706, 0xffff9806, 0xffff9906, 0xffff9a07, 0xffff9b07, 0xffff9d07, 0xffff9e08, 0xffff9f08, 0xffffa008, 0xffffa109, 0xffffa309, 0xffffa409, 0xffffa50a, 0xffffa60a, 0xffffa80a, 0xffffa90b,
        0xffffaa0b, 0xffffab0c, 0xffffac0c, 0xffffae0d, 0xffffaf0d, 0xffffb00e, 0xffffb10e, 0xffffb30f, 0xffffb40f, 0xffffb510, 0xffffb610, 0xffffb811, 0xffffb912, 0xffffba12, 0xffffbb13, 0xffffbd14,
        0xffffbe14, 0xffffbf15, 0xffffc016, 0xffffc217, 0xffffc317, 0xffffc418, 0xffffc619, 0xffffc71a, 0xffffc81b, 0xffffca1c, 0xffffcb1d, 0xffffcc1e, 0xffffcd1f, 0xffffcf20, 0xffffd021, 0xffffd122,
        0xffffd323, 0xffffd424, 0xffffd526, 0xffffd727, 0xffffd828, 0xffffd92a, 0xffffdb2b, 0xffffdc2c, 0xffffdd2e, 0xffffdf2f, 0xffffe031, 0xffffe133, 0xffffe334, 0xffffe436, 0xffffe538, 0xffffe739
    },
    { // Green monitor
        0xff000000, 0xff000400, 0xff000700, 0xff000900, 0xff000b00, 0xff000d00, 0xff000f00, 0xff001100, 0xff001300, 0xff001500, 0xff001600, 0xff001800, 0xff001a00, 0xff001b00, 0xff001d00, 0xff001e00,
        0xff002000, 0xff002100, 0xff002300, 0xff002400, 0xff002601, 0xff002701, 0xff002901, 0xff002a01, 0xff002b01, 0xff002d01, 0xff002e01, 0xff002f01, 0xff003101, 0xff003201, 0xff003301, 0xff003401,
        0xff003601, 0xff003702, 0xff003802, 0xff003902, 0xff003b02, 0xff003c02, 0xff003d02, 0xff003e02, 0xff004002, 0xff004102, 0xff004203, 0xff004303, 0xff004403, 0xff004503, 0xff004703, 0xff004803,
        0xff004903, 0xff004a03, 0xff004b04, 0xff004c04, 0xff004d04, 0xff004e04, 0xff005004, 0xff005104, 0xff005205, 0xff005305, 0xff005405, 0xff005505, 0xff005605, 0xff005705, 0xff005806, 0xff005906,
        0xff005a06, 0xff005b06, 0xff005d06, 0xff005e07, 0xff005f07, 0xff006007, 0xff006107, 0xff006207, 0xff006308, 0xff006408, 0xff006508, 0xff006608, 0xff006708, 0xff006809, 0xff006909, 0xff006a09,
        0xff006b09, 0xff016c0a, 0xff016d0a, 0xff016e0a, 0xff016f0a, 0xff01700b, 0xff01710b, 0xff01720b, 0xff01730b, 0xff01740c, 0xff01750c, 0xff01760c, 0xff01770c, 0xff01780d, 0xff01790d, 0xff017a0d,
        0xff017b0d, 0xff017b0e, 0xff017c0e, 0xff017d0e, 0xff017e0f, 0xff017f0f, 0xff01800f, 0xff018110, 0xff028210, 0xff028310, 0xff028410, 0xff028511, 0xff028611, 0xff028711, 0xff028812, 0xff028912,
        0xff028a12, 0xff028a13, 0xff028b13, 0xff028c13, 0xff028d14, 0xff028e14, 0xff038f14, 0xff039015, 0xff039115, 0xff039215, 0xff039316, 0xff039416, 0xff039417, 0xff039517, 0xff039617, 0xff039718,
        0xff049818, 0xff049918, 0xff049a19, 0xff049b19, 0xff049c19, 0xff049c1a, 0xff049d1a, 0xff049e1b, 0xff059f1b, 0xff05a01b, 0xff05a11c, 0xff05a21c, 0xff05a31c, 0xff05a31d, 0xff05a41d, 0xff06a51e,
        0xff06a61e, 0xff06a71f, 0xff06a81f, 0xff06a920, 0xff06aa20, 0xff07aa21, 0xff07ab21, 0xff07ac21, 0xff07ad22, 0xff07ae22, 0xff08af23, 0xff08b023, 0xff08b024, 0xff08b124, 0xff08b225, 0xff09b325,
        0xff09b426, 0xff09b526, 0xff09b527, 0xff0ab627, 0xff0ab728, 0xff0ab828, 0xff0ab929, 0xff0bba29, 0xff0bba2a, 0xff0bbb2a, 0xff0bbc2b, 0xff0cbd2b, 0xff0cbe2c, 0xff0cbf2c, 0xff0dbf2d, 0xff0dc02d,
        0xff0dc12e, 0xff0ec22e, 0xff0ec32f, 0xff0ec42f, 0xff0fc430, 0xff0fc530, 0xff0fc631, 0xff10c731, 0xff10c832, 0xff10c932, 0xff11c933, 0xff11ca33, 0xff11cb34, 0xff12cc35, 0xff12cd35, 0xff12cd36,
        0xff13ce36, 0xff13cf37, 0xff13d037, 0xff14d138, 0xff14d139, 0xff14d239, 0xff15d33a, 0xff15d43a, 0xff16d43b, 0xff16d53b, 0xff17d63c, 0xff17d73d, 0xff17d83d, 0xff18d83e, 0xff18d93e, 0xff19da3f,
        0xff19db40, 0xff1adc40, 0xff1adc41, 0xff1bdd41, 0xff1bde42, 0xff1cdf43, 0xff1ce043, 0xff1de044, 0xff1ee145, 0xff1ee245, 0xff1fe346, 0xff1fe446, 0xff20e447, 0xff20e548, 0xff21e648, 0xff22e749,
        0xff22e74a, 0xff23e84a, 0xff23e94b, 0xff24ea4c, 0xff25ea4c, 0xff25eb4d, 0xff26ec4e, 0xff27ed4e, 0xff27ee4f, 0xff28ee50, 0xff29ef50, 0xff29f051, 0xff2af152, 0xff2bf153, 0xff2cf253, 0xff2cf354,
        0xff2df455, 0xff2ef455, 0xff2ff556, 0xff2ff657, 0xff30f758, 0xff31f758, 0xff32f859, 0xff32f95a, 0xff33fa5a, 0xff34fa5b, 0xff35fb5c, 0xff36fc5d, 0xff37fd5d, 0xff38fd5e, 0xff38fe5f, 0xff39ff60
    },
    { // White monitor
        0xff000000, 0xff010102, 0xff020203, 0xff020304, 0xff030406, 0xff040507, 0xff050608, 0xff060709, 0xff07080a, 0xff08090c, 0xff080a0d, 0xff090b0e, 0xff0a0c0f, 0xff0b0d10, 0xff0c0e11, 0xff0d0f12,
        0xff0e1013, 0xff0f1115, 0xff101216, 0xff111317, 0xff121418, 0xff121519, 0xff13161a, 0xff14171b, 0xff15181c, 0xff16191d, 0xff171a1e, 0xff181b1f, 0xff191c20, 0xff1a1d21, 0xff1b1e22, 0xff1c1f23,
        0xff1d2024, 0xff1e2125, 0xff1f2226, 0xff202327, 0xff212428, 0xff222529, 0xff22262b, 0xff23272c, 0xff24282d, 0xff25292e, 0xff262a2f, 0xff272b30, 0xff282c30, 0xff292d31, 0xff2a2e32, 0xff2b2f33,
        0xff2c3034, 0xff2d3035, 0xff2e3136, 0xff2f3237, 0xff303338, 0xff313439, 0xff32353a, 0xff33363b, 0xff34373c, 0xff35383d, 0xff36393e, 0xff373a3f, 0xff383b40, 0xff393c41, 0xff3a3d42, 0xff3b3e43,
        0xff3c3f44, 0xff3d4045, 0xff3e4146, 0xff3f4247, 0xff404348, 0xff414449, 0xff42454a, 0xff43464b, 0xff44474c, 0xff45484d, 0xff46494d, 0xff474a4e, 0xff484b4f, 0xff484c50, 0xff494d51, 0xff4a4e52,
        0xff4b4f53, 0xff4c5054, 0xff4d5155, 0xff4e5256, 0xff4f5357, 0xff505458, 0xff515559, 0xff52565a, 0xff53575b, 0xff54585b, 0xff55595c, 0xff565a5d, 0xff575b5e, 0xff585c5f, 0xff595d60, 0xff5a5e61,
        0xff5b5f62, 0xff5c6063, 0xff5d6164, 0xff5e6265, 0xff5f6366, 0xff606466, 0xff616567, 0xff626668, 0xff636769, 0xff64686a, 0xff65696b, 0xff666a6c, 0xff676b6d, 0xff686c6e, 0xff696d6f, 0xff6a6e70,
        0xff6b6f70, 0xff6c7071, 0xff6d7172, 0xff6f7273, 0xff707374, 0xff707475, 0xff717576, 0xff727677, 0xff747778, 0xff757879, 0xff767979, 0xff777a7a, 0xff787b7b, 0xff797c7c, 0xff7a7d7d, 0xff7b7e7e,
        0xff7c7f7f, 0xff7d8080, 0xff7e8181, 0xff7f8281, 0xff808382, 0xff818483, 0xff828584, 0xff838685, 0xff848786, 0xff858887, 0xff868988, 0xff878a89, 0xff888b89, 0xff898c8a, 0xff8a8d8b, 0xff8b8e8c,
        0xff8c8f8d, 0xff8d8f8e, 0xff8e908f, 0xff8f9190, 0xff909290, 0xff919391, 0xff929492, 0xff939593, 0xff949694, 0xff959795, 0xff969896, 0xff979997, 0xff989a98, 0xff999b98, 0xff9a9c99, 0xff9b9d9a,
        0xff9c9e9b, 0xff9d9f9c, 0xff9ea09d, 0xff9fa19e, 0xffa0a29f, 0xffa1a39f, 0xffa2a4a0, 0xffa3a5a1, 0xffa4a6a2, 0xffa6a7a3, 0xffa7a8a4, 0xffa8a9a5, 0xffa9aaa5, 0xffaaaba6, 0xffabaca7, 0xffacada8,
        0xffadaea9, 0xffaeafaa, 0xffafb0ab, 0xffb0b1ac, 0xffb1b2ac, 0xffb2b3ad, 0xffb3b4ae, 0xffb4b5af, 0xffb5b6b0, 0xffb6b7b1, 0xffb7b8b2, 0xffb8b9b2, 0xffb9bab3, 0xffbabbb4, 0xffbbbcb5, 0xffbcbdb6,
        0xffbdbeb7, 0xffbebfb8, 0xffbfc0b8, 0xffc0c1b9, 0xffc1c2ba, 0xffc2c3bb, 0xffc3c4bc, 0xffc5c5bd, 0xffc6c6be, 0xffc7c7be, 0xffc8c8bf, 0xffc9c9c0, 0xffcacac1, 0xffcbcbc2, 0xffccccc3, 0xffcdcdc3,
        0xffcecec4, 0xffcfcfc5, 0xffd0d0c6, 0xffd1d1c7, 0xffd2d2c8, 0xffd3d3c9, 0xffd4d4c9, 0xffd5d5ca, 0xffd6d6cb, 0xffd7d7cc, 0xffd8d8cd, 0xffd9d9ce, 0xffdadacf, 0xffdbdbcf, 0xffdcdcd0, 0xffdeddd1,
        0xffdfded2, 0xffe0dfd3, 0xffe1e0d4, 0xffe2e1d4, 0xffe3e2d5, 0xffe4e3d6, 0xffe5e4d7, 0xffe6e5d8, 0xffe7e6d9, 0xffe8e7d9, 0xffe9e8da, 0xffeae9db, 0xffebeadc, 0xffecebdd, 0xffedecde, 0xffeeeddf,
        0xffefeedf, 0xfff0efe0, 0xfff1f0e1, 0xfff2f1e2, 0xfff3f2e3, 0xfff4f3e3, 0xfff6f3e4, 0xfff7f4e5, 0xfff8f5e6, 0xfff9f6e7, 0xfffaf7e8, 0xfffbf8e9, 0xfffcf9e9, 0xfffdfaea, 0xfffefbeb, 0xfffffcec
    }
};

typedef struct blit_data_struct {
    int x, y, w, h;
    int busy;
    int buffer_in_use;
    int thread_run;
    int monitor_index;

    thread_t *blit_thread;
    event_t  *wake_blit_thread;
    event_t  *blit_complete;
    event_t  *buffer_not_in_use;
} blit_data_t;

static uint32_t cga_2_table[16];

static void (*blit_func)(int x, int y, int w, int h, int monitor_index);

#ifdef ENABLE_VIDEO_LOG
int video_do_log = ENABLE_VIDEO_LOG;

static void
video_log(const char *fmt, ...)
{
    va_list ap;

    if (video_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define video_log(fmt, ...)
#endif

void
video_setblit(void (*blit)(int, int, int, int, int))
{
    blit_func = blit;
}

void
video_blit_complete_monitor(int monitor_index)
{
    blit_data_t *blit_data_ptr   = monitors[monitor_index].mon_blit_data_ptr;
    blit_data_ptr->buffer_in_use = 0;

    thread_set_event(blit_data_ptr->buffer_not_in_use);
}

void
video_wait_for_blit_monitor(int monitor_index)
{
    blit_data_t *blit_data_ptr = monitors[monitor_index].mon_blit_data_ptr;

    while (blit_data_ptr->busy)
        thread_wait_event(blit_data_ptr->blit_complete, -1);
    thread_reset_event(blit_data_ptr->blit_complete);
}

void
video_wait_for_buffer_monitor(int monitor_index)
{
    blit_data_t *blit_data_ptr = monitors[monitor_index].mon_blit_data_ptr;

    while (blit_data_ptr->buffer_in_use)
        thread_wait_event(blit_data_ptr->buffer_not_in_use, -1);
    thread_reset_event(blit_data_ptr->buffer_not_in_use);
}

static png_structp png_ptr[MONITORS_NUM];
static png_infop   info_ptr[MONITORS_NUM];

static void
video_take_screenshot_monitor(const char *fn, uint32_t *buf, int start_x, int start_y, int row_len, int monitor_index)
{
    png_bytep         *b_rgb         = NULL;
    FILE              *fp            = NULL;
    uint32_t           temp          = 0x00000000;
    const blit_data_t *blit_data_ptr = monitors[monitor_index].mon_blit_data_ptr;

    /* create file */
    fp = plat_fopen(fn, (const char *) "wb");
    if (!fp) {
        video_log("[video_take_screenshot] File %s could not be opened for writing", fn);
        return;
    }

    /* initialize stuff */
    png_ptr[monitor_index] = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!png_ptr[monitor_index]) {
        video_log("[video_take_screenshot] png_create_write_struct failed");
        fclose(fp);
        return;
    }

    info_ptr[monitor_index] = png_create_info_struct(png_ptr[monitor_index]);
    if (!info_ptr[monitor_index]) {
        video_log("[video_take_screenshot] png_create_info_struct failed");
        fclose(fp);
        return;
    }

    png_init_io(png_ptr[monitor_index], fp);

    png_set_IHDR(png_ptr[monitor_index], info_ptr[monitor_index], blit_data_ptr->w, blit_data_ptr->h,
                 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    b_rgb = (png_bytep *) malloc(sizeof(png_bytep) * blit_data_ptr->h);
    if (b_rgb == NULL) {
        video_log("[video_take_screenshot] Unable to Allocate RGB Bitmap Memory");
        fclose(fp);
        return;
    }

    for (int y = 0; y < blit_data_ptr->h; ++y) {
        b_rgb[y] = (png_byte *) malloc(png_get_rowbytes(png_ptr[monitor_index], info_ptr[monitor_index]));
        for (int x = 0; x < blit_data_ptr->w; ++x) {
            if (buf == NULL)
                memset(&(b_rgb[y][x * 3]), 0x00, 3);
            else {
                temp                  = buf[((start_y + y) * row_len) + start_x + x];
                b_rgb[y][x * 3]       = (temp >> 16) & 0xff;
                b_rgb[y][(x * 3) + 1] = (temp >> 8) & 0xff;
                b_rgb[y][(x * 3) + 2] = temp & 0xff;
            }
        }
    }

    png_write_info(png_ptr[monitor_index], info_ptr[monitor_index]);

    png_write_image(png_ptr[monitor_index], b_rgb);

    png_write_end(png_ptr[monitor_index], NULL);

    /* cleanup heap allocation */
    for (int i = 0; i < blit_data_ptr->h; i++)
        if (b_rgb[i])
            free(b_rgb[i]);

    if (b_rgb)
        free(b_rgb);

    if (fp)
        fclose(fp);
}

void
video_screenshot_monitor(uint32_t *buf, int start_x, int start_y, int row_len, int monitor_index)
{
    char path[1024];
    char fn[256];

    memset(fn, 0, sizeof(fn));
    memset(path, 0, sizeof(path));

    path_append_filename(path, usr_path, SCREENSHOT_PATH);

    if (!plat_dir_check(path))
        plat_dir_create(path);

    path_slash(path);
    strcat(path, "Monitor_");
    snprintf(&path[strlen(path)], 42, "%d_", monitor_index + 1);

    plat_tempfile(fn, NULL, ".png");
    strcat(path, fn);

    video_log("taking screenshot to: %s\n", path);

    video_take_screenshot_monitor((const char *) path, buf, start_x, start_y, row_len, monitor_index);
    png_destroy_write_struct(&png_ptr[monitor_index], &info_ptr[monitor_index]);

    atomic_fetch_sub(&monitors[monitor_index].mon_screenshots_raw, 1);
}

void
video_screenshot(uint32_t *buf, int start_x, int start_y, int row_len)
{
    video_screenshot_monitor(buf, start_x, start_y, row_len, 0);
}

#ifdef _WIN32
void *__cdecl video_transform_copy(void *_Dst, const void *_Src, size_t _Size)
#else
void *
video_transform_copy(void *__restrict _Dst, const void *__restrict _Src, size_t _Size)
#endif
{
    uint32_t       *dest_ex = (uint32_t *) _Dst;
    const uint32_t *src_ex  = (const uint32_t *) _Src;

    _Size /= sizeof(uint32_t);

    if ((dest_ex != NULL) && (src_ex != NULL)) {
        for (size_t i = 0; i < _Size; i++) {
            *dest_ex = video_color_transform(*src_ex);
            dest_ex++;
            src_ex++;
        }
    }

    return _Dst;
}

static void
blit_thread(void *param)
{
    blit_data_t *data = param;
    while (data->thread_run) {
        thread_wait_event(data->wake_blit_thread, -1);
        thread_reset_event(data->wake_blit_thread);
        MTR_BEGIN("video", "blit_thread");

        if (blit_func)
            blit_func(data->x, data->y, data->w, data->h, data->monitor_index);

        data->busy = 0;

        MTR_END("video", "blit_thread");
        thread_set_event(data->blit_complete);
    }
}

void
video_blit_memtoscreen_monitor(int x, int y, int w, int h, int monitor_index)
{
    MTR_BEGIN("video", "video_blit_memtoscreen");

    if ((w <= 0) || (h <= 0))
        return;

    video_wait_for_blit_monitor(monitor_index);

    monitors[monitor_index].mon_blit_data_ptr->busy          = 1;
    monitors[monitor_index].mon_blit_data_ptr->buffer_in_use = 1;
    monitors[monitor_index].mon_blit_data_ptr->x             = x;
    monitors[monitor_index].mon_blit_data_ptr->y             = y;
    monitors[monitor_index].mon_blit_data_ptr->w             = w;
    monitors[monitor_index].mon_blit_data_ptr->h             = h;
    monitors[monitor_index].mon_renderedframes++;

    thread_set_event(monitors[monitor_index].mon_blit_data_ptr->wake_blit_thread);
    MTR_END("video", "video_blit_memtoscreen");
}

uint8_t
pixels8(uint32_t *pixels)
{
    uint8_t temp = 0;

    for (uint8_t i = 0; i < 8; i++)
        temp |= (!!*(pixels + i) << (i ^ 7));

    return temp;
}

uint32_t
pixel_to_color(uint8_t *pixels32, uint8_t pos)
{
    uint32_t temp;
    temp = *(pixels32 + pos) & 0x03;
    switch (temp) {
        default:
        case 0:
            return 0x00;
        case 1:
            return 0x07;
        case 2:
            return 0x0f;
    }
}

void
video_blend_monitor(int x, int y, int monitor_index)
{
    uint32_t            pixels32_1;
    uint32_t            pixels32_2;
    unsigned int        val1;
    unsigned int        val2;
    static unsigned int carry = 0;

    if (!herc_blend)
        return;

    if (!x)
        carry = 0;

    val1       = pixels8(&(monitors[monitor_index].target_buffer->line[y][x]));
    val2       = (val1 >> 1) + carry;
    carry      = (val1 & 1) << 7;
    pixels32_1 = cga_2_table[val1 >> 4] + cga_2_table[val2 >> 4];
    pixels32_2 = cga_2_table[val1 & 0xf] + cga_2_table[val2 & 0xf];
    for (uint8_t xx = 0; xx < 4; xx++) {
        monitors[monitor_index].target_buffer->line[y][x + xx]       = pixel_to_color((uint8_t *) &pixels32_1, xx);
        monitors[monitor_index].target_buffer->line[y][x + (xx | 4)] = pixel_to_color((uint8_t *) &pixels32_2, xx);
    }
}

void
video_process_8_monitor(int x, int y, int monitor_index)
{
    for (int xx = 0; xx < x; xx++) {
        if (monitors[monitor_index].target_buffer->line[y][xx] <= 0xff)
            monitors[monitor_index].target_buffer->line[y][xx] = monitors[monitor_index].mon_pal_lookup[monitors[monitor_index].target_buffer->line[y][xx]];
        else
            monitors[monitor_index].target_buffer->line[y][xx] = 0x00000000;
    }
}

void
cgapal_rebuild_monitor(int monitor_index)
{
    int       c;
    uint32_t *palette_lookup      = monitors[monitor_index].mon_pal_lookup;
    int       cga_palette_monitor = 0;

    /* We cannot do this (yet) if we have not been enabled yet. */
    if (video_6to8 == NULL)
        return;

    if (monitors[monitor_index].target_buffer == NULL || monitors[monitor_index].mon_cga_palette == NULL)
        return;

    cga_palette_monitor = *monitors[monitor_index].mon_cga_palette;

    for (c = 0; c < 256; c++) {
        palette_lookup[c] = makecol(video_6to8[cgapal[c].r],
                                    video_6to8[cgapal[c].g],
                                    video_6to8[cgapal[c].b]);
    }

    if ((cga_palette_monitor > 1) && (cga_palette_monitor < 7)) {
        if (vid_cga_contrast != 0) {
            for (c = 0; c < 16; c++) {
                palette_lookup[c]      = makecol(video_6to8[cgapal_mono[cga_palette_monitor - 2][c].r],
                                                 video_6to8[cgapal_mono[cga_palette_monitor - 2][c].g],
                                                 video_6to8[cgapal_mono[cga_palette_monitor - 2][c].b]);
                palette_lookup[c + 16] = makecol(video_6to8[cgapal_mono[cga_palette_monitor - 2][c].r],
                                                 video_6to8[cgapal_mono[cga_palette_monitor - 2][c].g],
                                                 video_6to8[cgapal_mono[cga_palette_monitor - 2][c].b]);
                palette_lookup[c + 32] = makecol(video_6to8[cgapal_mono[cga_palette_monitor - 2][c].r],
                                                 video_6to8[cgapal_mono[cga_palette_monitor - 2][c].g],
                                                 video_6to8[cgapal_mono[cga_palette_monitor - 2][c].b]);
                palette_lookup[c + 48] = makecol(video_6to8[cgapal_mono[cga_palette_monitor - 2][c].r],
                                                 video_6to8[cgapal_mono[cga_palette_monitor - 2][c].g],
                                                 video_6to8[cgapal_mono[cga_palette_monitor - 2][c].b]);
            }
        } else {
            for (c = 0; c < 16; c++) {
                palette_lookup[c]      = makecol(video_6to8[cgapal_mono[cga_palette_monitor - 1][c].r],
                                                 video_6to8[cgapal_mono[cga_palette_monitor - 1][c].g],
                                                 video_6to8[cgapal_mono[cga_palette_monitor - 1][c].b]);
                palette_lookup[c + 16] = makecol(video_6to8[cgapal_mono[cga_palette_monitor - 1][c].r],
                                                 video_6to8[cgapal_mono[cga_palette_monitor - 1][c].g],
                                                 video_6to8[cgapal_mono[cga_palette_monitor - 1][c].b]);
                palette_lookup[c + 32] = makecol(video_6to8[cgapal_mono[cga_palette_monitor - 1][c].r],
                                                 video_6to8[cgapal_mono[cga_palette_monitor - 1][c].g],
                                                 video_6to8[cgapal_mono[cga_palette_monitor - 1][c].b]);
                palette_lookup[c + 48] = makecol(video_6to8[cgapal_mono[cga_palette_monitor - 1][c].r],
                                                 video_6to8[cgapal_mono[cga_palette_monitor - 1][c].g],
                                                 video_6to8[cgapal_mono[cga_palette_monitor - 1][c].b]);
            }
        }
    }

    if (cga_palette_monitor == 8)
        palette_lookup[0x16] = makecol(video_6to8[42], video_6to8[42], video_6to8[0]);
    else if (cga_palette_monitor == 10) {
        /* IBM 5153 CRT, colors by VileR */
        palette_lookup[0x10] = 0xff000000;
        palette_lookup[0x11] = 0xff0000c4;
        palette_lookup[0x12] = 0xff00c400;
        palette_lookup[0x13] = 0xff00c4c4;
        palette_lookup[0x14] = 0xffc40000;
        palette_lookup[0x15] = 0xffc400c4;
        palette_lookup[0x16] = 0xffc47e00;
        palette_lookup[0x17] = 0xffc4c4c4;
        palette_lookup[0x18] = 0xff4e4e4e;
        palette_lookup[0x19] = 0xff4e4edc;
        palette_lookup[0x1a] = 0xff4edc4e;
        palette_lookup[0x1b] = 0xff4ef3f3;
        palette_lookup[0x1c] = 0xffdc4e4e;
        palette_lookup[0x1d] = 0xfff34ef3;
        palette_lookup[0x1e] = 0xfff3f34e;
        palette_lookup[0x1f] = 0xffffffff;
    }
}

void
video_inform_monitor(int type, const video_timings_t *ptr, int monitor_index)
{
    monitor_t *monitor       = &monitors[monitor_index];
    monitor->mon_vid_type    = type;
    monitor->mon_vid_timings = ptr;
}

int
video_get_type_monitor(int monitor_index)
{
    return monitors[monitor_index].mon_vid_type;
}

void
video_update_timing(void)
{
    const video_timings_t *monitor_vid_timings = NULL;
    int                   *vid_timing_read_b   = NULL;
    int                   *vid_timing_read_l   = NULL;
    int                   *vid_timing_read_w   = NULL;
    int                   *vid_timing_write_b  = NULL;
    int                   *vid_timing_write_l  = NULL;
    int                   *vid_timing_write_w  = NULL;

    for (uint8_t i = 0; i < MONITORS_NUM; i++) {
        monitor_vid_timings = monitors[i].mon_vid_timings;
        if (!monitor_vid_timings)
            continue;
        vid_timing_read_b  = &monitors[i].mon_video_timing_read_b;
        vid_timing_read_l  = &monitors[i].mon_video_timing_read_l;
        vid_timing_read_w  = &monitors[i].mon_video_timing_read_w;
        vid_timing_write_b = &monitors[i].mon_video_timing_write_b;
        vid_timing_write_l = &monitors[i].mon_video_timing_write_l;
        vid_timing_write_w = &monitors[i].mon_video_timing_write_w;

        if (monitor_vid_timings->type == VIDEO_ISA) {
            *vid_timing_read_b  = ISA_CYCLES(monitor_vid_timings->read_b);
            *vid_timing_read_w  = ISA_CYCLES(monitor_vid_timings->read_w);
            *vid_timing_read_l  = ISA_CYCLES(monitor_vid_timings->read_l);
            *vid_timing_write_b = ISA_CYCLES(monitor_vid_timings->write_b);
            *vid_timing_write_w = ISA_CYCLES(monitor_vid_timings->write_w);
            *vid_timing_write_l = ISA_CYCLES(monitor_vid_timings->write_l);
        } else if (monitor_vid_timings->type == VIDEO_PCI) {
            *vid_timing_read_b  = (int) (pci_timing * monitor_vid_timings->read_b);
            *vid_timing_read_w  = (int) (pci_timing * monitor_vid_timings->read_w);
            *vid_timing_read_l  = (int) (pci_timing * monitor_vid_timings->read_l);
            *vid_timing_write_b = (int) (pci_timing * monitor_vid_timings->write_b);
            *vid_timing_write_w = (int) (pci_timing * monitor_vid_timings->write_w);
            *vid_timing_write_l = (int) (pci_timing * monitor_vid_timings->write_l);
        } else if (monitor_vid_timings->type == VIDEO_AGP) {
            *vid_timing_read_b  = (int) (agp_timing * monitor_vid_timings->read_b);
            *vid_timing_read_w  = (int) (agp_timing * monitor_vid_timings->read_w);
            *vid_timing_read_l  = (int) (agp_timing * monitor_vid_timings->read_l);
            *vid_timing_write_b = (int) (agp_timing * monitor_vid_timings->write_b);
            *vid_timing_write_w = (int) (agp_timing * monitor_vid_timings->write_w);
            *vid_timing_write_l = (int) (agp_timing * monitor_vid_timings->write_l);
        } else {
            *vid_timing_read_b  = (int) (bus_timing * monitor_vid_timings->read_b);
            *vid_timing_read_w  = (int) (bus_timing * monitor_vid_timings->read_w);
            *vid_timing_read_l  = (int) (bus_timing * monitor_vid_timings->read_l);
            *vid_timing_write_b = (int) (bus_timing * monitor_vid_timings->write_b);
            *vid_timing_write_w = (int) (bus_timing * monitor_vid_timings->write_w);
            *vid_timing_write_l = (int) (bus_timing * monitor_vid_timings->write_l);
        }

        if (cpu_16bitbus) {
            *vid_timing_read_l  = *vid_timing_read_w * 2;
            *vid_timing_write_l = *vid_timing_write_w * 2;
        }
    }
}

int
calc_6to8(int c)
{
    int    ic;
    int    i8;
    double d8;

    ic = c;
    if (ic == 64)
        ic = 63;
    else
        ic &= 0x3f;
    d8 = (ic / 63.0) * 255.0;
    i8 = (int) d8;

    return (i8 & 0xff);
}

int
calc_8to32(int c)
{
    int    b;
    int    g;
    int    r;
    double db;
    double dg;
    double dr;

    b  = (c & 3);
    g  = ((c >> 2) & 7);
    r  = ((c >> 5) & 7);
    db = (((double) b) / 3.0) * 255.0;
    dg = (((double) g) / 7.0) * 255.0;
    dr = (((double) r) / 7.0) * 255.0;
    b  = (int) db;
    g  = ((int) dg) << 8;
    r  = ((int) dr) << 16;

    return (b | g | r | 0xff000000);
}

int
calc_15to32(int c)
{
    int    b;
    int    g;
    int    r;
    double db;
    double dg;
    double dr;

    b  = (c & 31);
    g  = ((c >> 5) & 31);
    r  = ((c >> 10) & 31);
    db = (((double) b) / 31.0) * 255.0;
    dg = (((double) g) / 31.0) * 255.0;
    dr = (((double) r) / 31.0) * 255.0;
    b  = (int) db;
    g  = ((int) dg) << 8;
    r  = ((int) dr) << 16;

    return (b | g | r | 0xff000000);
}

int
calc_16to32(int c)
{
    int    b;
    int    g;
    int    r;
    double db;
    double dg;
    double dr;

    b  = (c & 31);
    g  = ((c >> 5) & 63);
    r  = ((c >> 11) & 31);
    db = (((double) b) / 31.0) * 255.0;
    dg = (((double) g) / 63.0) * 255.0;
    dr = (((double) r) / 31.0) * 255.0;
    b  = (int) db;
    g  = ((int) dg) << 8;
    r  = ((int) dr) << 16;

    return (b | g | r | 0xff000000);
}

void
hline(bitmap_t *b, int x1, int y, int x2, uint32_t col)
{
    if (y < 0 || y >= b->h)
        return;

    for (int x = x1; x < x2; x++)
        b->line[y][x] = col;
}

void
destroy_bitmap(bitmap_t *b)
{
    if ((b != NULL) && (b->dat != NULL))
        free(b->dat);

    if (b != NULL)
        free(b);
}

bitmap_t *
create_bitmap(int x, int y)
{
    bitmap_t *b = calloc(sizeof(bitmap_t), (y * sizeof(uint32_t *)));

    b->dat = calloc((size_t) x * y, 4);
    for (int c = 0; c < y; c++)
        b->line[c] = &(b->dat[c * x]);
    b->w = x;
    b->h = y;

    return b;
}

void
video_monitor_init(int index)
{
    memset(&monitors[index], 0, sizeof(monitor_t));
    monitors[index].mon_xsize                            = 640;
    monitors[index].mon_ysize                            = 480;
    monitors[index].mon_res_x                            = 640;
    monitors[index].mon_res_y                            = 480;
    monitors[index].mon_scrnsz_x                         = 640;
    monitors[index].mon_scrnsz_y                         = 480;
    monitors[index].mon_efscrnsz_y                       = 480;
    monitors[index].mon_unscaled_size_x                  = 480;
    monitors[index].mon_unscaled_size_y                  = 480;
    monitors[index].mon_bpp                              = 8;
    monitors[index].mon_changeframecount                 = 2;
    monitors[index].target_buffer                        = create_bitmap(2048, 2048);
    monitors[index].mon_blit_data_ptr                    = calloc(1, sizeof(blit_data_t));
    monitors[index].mon_blit_data_ptr->wake_blit_thread  = thread_create_event();
    monitors[index].mon_blit_data_ptr->blit_complete     = thread_create_event();
    monitors[index].mon_blit_data_ptr->buffer_not_in_use = thread_create_event();
    monitors[index].mon_blit_data_ptr->thread_run        = 1;
    monitors[index].mon_blit_data_ptr->monitor_index     = index;
    monitors[index].mon_pal_lookup                       = calloc(sizeof(uint32_t), 256);
    monitors[index].mon_cga_palette                      = calloc(1, sizeof(int));
    monitors[index].mon_force_resize                     = 1;
    monitors[index].mon_vid_type                         = VIDEO_FLAG_TYPE_NONE;
    atomic_init(&doresize_monitors[index], 0);
    atomic_init(&monitors[index].mon_screenshots, 0);
    atomic_init(&monitors[index].mon_screenshots_clipboard, 0);
    atomic_init(&monitors[index].mon_screenshots_raw, 0);
    atomic_init(&monitors[index].mon_screenshots_raw_clipboard, 0);
    if (index >= 1)
        ui_init_monitor(index);
    monitors[index].mon_blit_data_ptr->blit_thread = thread_create(blit_thread, monitors[index].mon_blit_data_ptr);
}

void
video_monitor_close(int monitor_index)
{
    if (monitors[monitor_index].target_buffer == NULL) {
        return;
    }
    monitors[monitor_index].mon_blit_data_ptr->thread_run = 0;
    thread_set_event(monitors[monitor_index].mon_blit_data_ptr->wake_blit_thread);
    thread_wait(monitors[monitor_index].mon_blit_data_ptr->blit_thread);
    if (monitor_index >= 1)
        ui_deinit_monitor(monitor_index);
    thread_destroy_event(monitors[monitor_index].mon_blit_data_ptr->buffer_not_in_use);
    thread_destroy_event(monitors[monitor_index].mon_blit_data_ptr->blit_complete);
    thread_destroy_event(monitors[monitor_index].mon_blit_data_ptr->wake_blit_thread);
    free(monitors[monitor_index].mon_blit_data_ptr);
    if (!monitors[monitor_index].mon_pal_lookup_static)
        free(monitors[monitor_index].mon_pal_lookup);
    if (!monitors[monitor_index].mon_cga_palette_static)
        free(monitors[monitor_index].mon_cga_palette);
    destroy_bitmap(monitors[monitor_index].target_buffer);
    monitors[monitor_index].target_buffer = NULL;
    memset(&monitors[monitor_index], 0, sizeof(monitor_t));
}

void
video_init(void)
{
    uint8_t total[2] = { 0, 1 };

    for (uint8_t c = 0; c < 16; c++) {
        cga_2_table[c] = (total[(c >> 3) & 1] << 0) | (total[(c >> 2) & 1] << 8) | (total[(c >> 1) & 1] << 16) | (total[(c >> 0) & 1] << 24);
    }

    for (uint8_t c = 0; c < 64; c++) {
        cgapal[c + 64].r = (((c & 4) ? 2 : 0) | ((c & 0x10) ? 1 : 0)) * 21;
        cgapal[c + 64].g = (((c & 2) ? 2 : 0) | ((c & 0x10) ? 1 : 0)) * 21;
        cgapal[c + 64].b = (((c & 1) ? 2 : 0) | ((c & 0x10) ? 1 : 0)) * 21;
        if ((c & 0x17) == 6)
            cgapal[c + 64].g >>= 1;
    }
    for (uint8_t c = 0; c < 64; c++) {
        cgapal[c + 128].r = (((c & 4) ? 2 : 0) | ((c & 0x20) ? 1 : 0)) * 21;
        cgapal[c + 128].g = (((c & 2) ? 2 : 0) | ((c & 0x10) ? 1 : 0)) * 21;
        cgapal[c + 128].b = (((c & 1) ? 2 : 0) | ((c & 0x08) ? 1 : 0)) * 21;
    }

    for (uint8_t c = 0; c < 4; c++) {
        for (uint8_t d = 0; d < 4; d++) {
            edatlookup[c][d] = 0;
            if (c & 1)
                edatlookup[c][d] |= 1;
            if (d & 1)
                edatlookup[c][d] |= 2;
            if (c & 2)
                edatlookup[c][d] |= 0x10;
            if (d & 2)
                edatlookup[c][d] |= 0x20;
        }
    }

    for (uint16_t c = 0; c < 256; c++) {
        egaremap2bpp[c] = 0;
        if (c & 0x01)
            egaremap2bpp[c] |= 0x01;
        if (c & 0x04)
            egaremap2bpp[c] |= 0x02;
        if (c & 0x10)
            egaremap2bpp[c] |= 0x04;
        if (c & 0x40)
            egaremap2bpp[c] |= 0x08;
    }

    video_6to8 = malloc(4 * 256);
    for (uint16_t c = 0; c < 256; c++)
        video_6to8[c] = calc_6to8(c);

    video_8togs = malloc(4 * 256);
    for (uint16_t c = 0; c < 256; c++)
        video_8togs[c] = c | (c << 16) | (c << 24);

    video_8to32 = malloc(4 * 256);
    for (uint16_t c = 0; c < 256; c++)
        video_8to32[c] = calc_8to32(c);

    video_15to32 = malloc(4 * 65536);
    for (uint32_t c = 0; c < 65536; c++)
        video_15to32[c] = calc_15to32(c & 0x7fff);

    video_16to32 = malloc(4 * 65536);
    for (uint32_t c = 0; c < 65536; c++)
        video_16to32[c] = calc_16to32(c);

    memset(monitors, 0, sizeof(monitors));
    video_monitor_init(0);
}

void
video_close(void)
{
    video_monitor_close(0);

    free(video_16to32);
    free(video_15to32);
    free(video_8to32);
    free(video_8togs);
    free(video_6to8);

    if (fontdatksc5601) {
        free(fontdatksc5601);
        fontdatksc5601 = NULL;
    }

    if (fontdatksc5601_user) {
        free(fontdatksc5601_user);
        fontdatksc5601_user = NULL;
    }
}

uint8_t
video_force_resize_get_monitor(int monitor_index)
{
    return monitors[monitor_index].mon_force_resize;
}

void
video_force_resize_set_monitor(uint8_t res, int monitor_index)
{
    monitors[monitor_index].mon_force_resize = res;
}

void
video_load_font(char *fn, int format, int offset)
{
    FILE *fp;

    fp = rom_fopen(fn, "rb");
    if (fp == NULL)
        return;

    fseek(fp, offset, SEEK_SET);

    switch (format) {
        case FONT_FORMAT_MDA: /* MDA */
            for (uint16_t c = 0; c < 256; c++) /* 8x14 MDA in 8x8 cell (lines 0-7) */
                for (uint8_t d = 0; d < 8; d++)
                    fontdatm[c][d] = fgetc(fp) & 0xff;
            for (uint16_t c = 0; c < 256; c++) /* 8x14 MDA in 8x8 cell (lines 8-13 + padding lines) */
                for (uint8_t d = 0; d < 8; d++)
                    fontdatm[c][d + 8] = fgetc(fp) & 0xff;
            (void) fseek(fp, 4096 + 2048, SEEK_SET);
            for (uint16_t c = 0; c < 256; c++) /* 8x8 CGA (thick, primary) */
                for (uint8_t d = 0; d < 8; d++)
                    fontdat[c][d] = fgetc(fp) & 0xff;
            break;

        case FONT_FORMAT_PC200: /* PC200 */
            for (uint8_t d = 0; d < 4; d++) {
                /* There are 4 fonts in the ROM */
                for (uint16_t c = 0; c < 256; c++) /* 8x14 MDA in 8x16 cell */
                    (void) !fread(&fontdatm[256 * d + c][0], 1, 16, fp);
                for (uint16_t c = 0; c < 256; c++) { /* 8x8 CGA in 8x16 cell */
                    (void) !fread(&fontdat[256 * d + c][0], 1, 8, fp);
                    fseek(fp, 8, SEEK_CUR);
                }
            }
            break;

        case FONT_FORMAT_CGA: /* CGA */
            for (uint16_t c = 0; c < 256; c++)
                for (uint8_t d = 0; d < 8; d++)
                    fontdat[c][d] = fgetc(fp) & 0xff;
            break;

        case FONT_FORMAT_WY700: /* Wyse 700 */
            for (uint16_t c = 0; c < 512; c++)
                for (uint8_t d = 0; d < 32; d++)
                    fontdatw[c][d] = fgetc(fp) & 0xff;
            break;

        case FONT_FORMAT_MDSI_GENIUS: /* MDSI Genius */
            for (uint16_t c = 0; c < 256; c++)
                for (uint8_t d = 0; d < 16; d++)
                    fontdat8x12[c][d] = fgetc(fp) & 0xff;
            break;

        case FONT_FORMAT_TOSHIBA_3100E: /* Toshiba 3100e */
            for (uint16_t d = 0; d < 2048; d += 512) { /* Four languages... */
                for (uint16_t c = d; c < d + 256; c++) {
                    (void) !fread(&fontdatm[c][8], 1, 8, fp);
                }
                for (uint32_t c = d + 256; c < d + 512; c++) {
                    (void) !fread(&fontdatm[c][8], 1, 8, fp);
                }
                for (uint32_t c = d; c < d + 256; c++) {
                    (void) !fread(&fontdatm[c][0], 1, 8, fp);
                }
                for (uint32_t c = d + 256; c < d + 512; c++) {
                    (void) !fread(&fontdatm[c][0], 1, 8, fp);
                }
                fseek(fp, 4096, SEEK_CUR); /* Skip blank section */
                for (uint32_t c = d; c < d + 256; c++) {
                    (void) !fread(&fontdat[c][0], 1, 8, fp);
                }
                for (uint32_t c = d + 256; c < d + 512; c++) {
                    (void) !fread(&fontdat[c][0], 1, 8, fp);
                }
            }
            break;

        case FONT_FORMAT_KSC6501: /* Korean KSC-5601 */
            if (!fontdatksc5601)
                fontdatksc5601 = malloc(16384 * sizeof(dbcs_font_t));

            if (!fontdatksc5601_user)
                fontdatksc5601_user = malloc(192 * sizeof(dbcs_font_t));

            for (uint32_t c = 0; c < 16384; c++) {
                for (uint8_t d = 0; d < 32; d++)
                    fontdatksc5601[c].chr[d] = fgetc(fp) & 0xff;
            }
            break;

        case FONT_FORMAT_SIGMA: /* Sigma Color 400 */
            /* The first 4k of the character ROM holds an 8x8 font */
            for (uint16_t c = 0; c < 256; c++) {
                (void) !fread(&fontdat[c][0], 1, 8, fp);
                fseek(fp, 8, SEEK_CUR);
            }
            /* The second 4k holds an 8x16 font */
            for (uint16_t c = 0; c < 256; c++) {
                if (fread(&fontdatm[c][0], 1, 16, fp) != 16)
                    fatal("video_load_font(): Error reading 8x16 font in Sigma Color 400 mode, c = %i\n", c);
            }
            break;

        case FONT_FORMAT_PC1512_T1000: /* Amstrad PC1512, Toshiba T1000/T1200 */
            for (uint16_t c = 0; c < 2048; c++) /* Allow up to 2048 chars */
                for (uint8_t d = 0; d < 8; d++)
                    fontdat[c][d] = fgetc(fp) & 0xff;
            break;

        case FONT_FORMAT_IM1024: /* Image Manager 1024 native font */
            for (uint16_t c = 0; c < 256; c++)
                (void) !fread(&fontdat12x18[c][0], 1, 36, fp);
            break;

        case FONT_FORMAT_PRAVETZ: /* Pravetz */
            for (uint16_t c = 0; c < 1024; c++) /* Allow up to 1024 chars */
                for (uint8_t d = 0; d < 8; d++)
                    fontdat[c][d] = fgetc(fp) & 0xff;
            break;

    }

    (void) fclose(fp);
}

uint32_t
video_color_transform(uint32_t color)
{
    uint8_t *clr8 = (uint8_t *) &color;
#if 0
    if (!video_grayscale && !invert_display)
        return color;
#endif
    if (video_grayscale) {
        if (video_graytype) {
            if (video_graytype == 1)
                color = ((54 * (uint32_t) clr8[2]) + (183 * (uint32_t) clr8[1]) + (18 * (uint32_t) clr8[0])) / 255;
            else
                color = ((uint32_t) clr8[2] + (uint32_t) clr8[1] + (uint32_t) clr8[0]) / 3;
        } else
            color = ((76 * (uint32_t) clr8[2]) + (150 * (uint32_t) clr8[1]) + (29 * (uint32_t) clr8[0])) / 255;
        switch (video_grayscale) {
            case 2:
            case 3:
            case 4:
                color = shade[video_grayscale][color];
                break;
            default:
                clr8[3] = 0xff;
                clr8[0] = color;
                clr8[1] = clr8[2] = clr8[0];
                break;
        }
    }
    if (invert_display)
        color ^= 0x00ffffff;
    return color;
}
