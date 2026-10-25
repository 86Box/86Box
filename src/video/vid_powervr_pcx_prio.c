/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          PowerVR PCX1/PCX2: lower the priority of the calling render
 *          thread, so that when the host's cores are busy the emulation
 *          thread is served first and the renderer takes what is left.
 *          Kept apart from the device so the system headers do not meet
 *          86Box's own.
 *
 * Authors: Zvi Rosen and contributors.
 *
 *          Copyright 2026 Zvi Rosen.
 */
#if defined(_WIN32)
#    include <windows.h>
#elif defined(__APPLE__)
#    include <pthread.h>
#    include <pthread/qos.h>
#elif defined(__linux__)
#    ifndef _GNU_SOURCE
#        define _GNU_SOURCE
#    endif
#    include <sys/resource.h>
#    include <sys/syscall.h>
#    include <unistd.h>
#endif

void pcx_lower_thread_priority(void);

void
pcx_lower_thread_priority(void)
{
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#elif defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#elif defined(__linux__)
    /* On Linux the nice value is per thread. */
    (void) setpriority(PRIO_PROCESS, (id_t) syscall(SYS_gettid), 5);
#endif
}
