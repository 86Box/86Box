/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for the implementation of CPU I/O ports used
 *          by NEC PC-98x1 machines.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2019 Miran Grca.
 */

#ifndef EMU_PORT_PC98_CPU_H
#define EMU_PORT_PC98_CPU_H

#ifdef _TIMER_H_
typedef struct port_pc98_cpu_t {
    pc_timer_t pulse_timer;

    uint64_t pulse_period;
} port_92_t;
#endif

extern void port_pc98_cpu_add(void *priv);
extern void port_pc98_cpu_remove(void *priv);

extern const device_t port_pc98_cpu_device;

#endif /*EMU_PORT_PC98_CPU_H*/
