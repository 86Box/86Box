/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          FPU type handler.
 *
 * Authors: Sarah Walker, <tommowalker@tommowalker.co.uk>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"


#ifdef ENABLE_FPU_LOG
int fpu_do_log = ENABLE_FPU_LOG;


void
fpu_log(const char *fmt, ...)
{
    va_list ap;

    if (fpu_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define fpu_log(fmt, ...)
#endif


int
fpu_get_type(const cpu_family_t *cpu_family, int cpu, const char *internal_name)
{
    const CPU *cpu_s = &cpu_family->cpus[cpu];
    const FPU *fpus = cpu_s->fpus;
    int fpu_type = fpus[0].type;
    int c = 0;

    while (fpus[c].internal_name) {
        if (!strcmp(internal_name, fpus[c].internal_name))
            fpu_type = fpus[c].type;
        c++;
    }

    return fpu_type;
}


const char *
fpu_get_internal_name(const cpu_family_t *cpu_family, int cpu, int type)
{
    const CPU *cpu_s = &cpu_family->cpus[cpu];
    const FPU *fpus = cpu_s->fpus;
    int c = 0;

    while (fpus[c].internal_name) {
        if (fpus[c].type == type)
            return fpus[c].internal_name;
        c++;
    }

    return fpus[0].internal_name;
}


const char *
fpu_get_name_from_index(const cpu_family_t *cpu_family, int cpu, int c)
{
    const CPU *cpu_s = &cpu_family->cpus[cpu];
    const FPU *fpus = cpu_s->fpus;

    return fpus[c].name;
}


int
fpu_get_type_from_index(const cpu_family_t *cpu_family, int cpu, int c)
{
    const CPU *cpu_s = &cpu_family->cpus[cpu];
    const FPU *fpus = cpu_s->fpus;

    return fpus[c].type;
}
