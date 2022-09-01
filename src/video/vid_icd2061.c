/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		ICD2061 clock generator emulation.
 *		Also emulates the ICS9161 which is the same as the ICD2016,
 *		but without the need for tuning (which is irrelevant in
 *		emulation anyway).
 *
 *		Used by ET4000w32/p (Diamond Stealth 32) and the S3
 *		Vision964 family.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>

typedef struct icd2061_t {
    float freq[3];

    int count, bit_count,
        unlocked, state;
    uint32_t data, ctrl;
} icd2061_t;

#ifdef ENABLE_ICD2061_LOG
int icd2061_do_log = ENABLE_ICD2061_LOG;

static void
icd2061_log(const char *fmt, ...)
{
    va_list ap;

    if (icd2061_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define icd2061_log(fmt, ...)
#endif

void
icd2061_write(void *p, int val)
{
    icd2061_t *icd2061 = (icd2061_t *) p;

    int nd, oc, nc;
    int a, qa, q, pa, p_, m, ps;

    nd = (val & 2) >> 1;     /* Old data. */
    oc = icd2061->state & 1; /* Old clock. */
    nc = val & 1;            /* New clock. */

    icd2061->state = val;

    if (nc && !oc) { /* Low-to-high transition of CLK. */
        if (!icd2061->unlocked) {
            if (nd) { /* DATA high. */
                icd2061->count++;
                icd2061_log("Low-to-high transition of CLK with DATA high, %i total\n", icd2061->count);
            } else { /* DATA low. */
                if (icd2061->count >= 5) {
                    icd2061->unlocked  = 1;
                    icd2061->bit_count = icd2061->data = 0;
#ifdef ENABLE_ICD2061_LOG
                    icd2061_log("ICD2061 unlocked\n");
#endif
                } else {
                    icd2061->count = 0;
#ifdef ENABLE_ICD2061_LOG
                    icd2061_log("ICD2061 locked\n");
#endif
                }
            }
        } else if (nc) {
            icd2061->data |= (nd << icd2061->bit_count);
            icd2061->bit_count++;

            if (icd2061->bit_count == 26) {
                icd2061_log("26 bits received, data = %08X\n", icd2061->data);

                a = ((icd2061->data >> 22) & 0x07); /* A  */
                icd2061_log("A = %01X\n", a);

                if (a < 3) {
                    pa = ((icd2061->data >> 11) & 0x7f); /* P' (ICD2061) / N' (ICS9161) */
                    m  = ((icd2061->data >> 8) & 0x07);  /* M  (ICD2061) / R  (ICS9161) */
                    qa = ((icd2061->data >> 1) & 0x7f);  /* Q' (ICD2061) / M' (ICS9161) */

                    p_ = pa + 3; /* P  (ICD2061) / N  (ICS9161) */
                    m  = 1 << m;
                    q  = qa + 2;                             /* Q  (ICD2061) / M  (ICS9161) */
                    ps = (icd2061->ctrl & (1 << a)) ? 4 : 2; /* Prescale */

                    icd2061->freq[a] = ((float) (p_ * ps) / (float) (q * m)) * 14318184.0f;

                    icd2061_log("P = %02X, M = %01X, Q = %02X, freq[%i] = %f\n", p_, m, q, a, icd2061->freq[a]);
                } else if (a == 6) {
                    icd2061->ctrl = ((icd2061->data >> 13) & 0xff);
                    icd2061_log("ctrl = %02X\n", icd2061->ctrl);
                }
                icd2061->count = icd2061->bit_count = icd2061->data = 0;
                icd2061->unlocked                                   = 0;
#ifdef ENABLE_ICD2061_LOG
                icd2061_log("ICD2061 locked\n");
#endif
            }
        }
    }
}

float
icd2061_getclock(int clock, void *p)
{
    icd2061_t *icd2061 = (icd2061_t *) p;

    if (clock > 2)
        clock = 2;

    return icd2061->freq[clock];
}

static void *
icd2061_init(const device_t *info)
{
    icd2061_t *icd2061 = (icd2061_t *) malloc(sizeof(icd2061_t));
    memset(icd2061, 0, sizeof(icd2061_t));

    icd2061->freq[0] = 25175000.0;
    icd2061->freq[1] = 28322000.0;
    icd2061->freq[2] = 28322000.0;

    return icd2061;
}

static void
icd2061_close(void *priv)
{
    icd2061_t *icd2061 = (icd2061_t *) priv;

    if (icd2061)
        free(icd2061);
}

const device_t icd2061_device = {
    .name          = "ICD2061 Clock Generator",
    .internal_name = "icd2061",
    .flags         = 0,
    .local         = 0,
    .init          = icd2061_init,
    .close         = icd2061_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ics9161_device = {
    .name          = "ICS9161 Clock Generator",
    .internal_name = "ics9161",
    .flags         = 0,
    .local         = 0,
    .init          = icd2061_init,
    .close         = icd2061_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
