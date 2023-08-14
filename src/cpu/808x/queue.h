/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Prefetch queue implementation header.
 *
 * Authors: gloriouscow, <https://github.com/dbalsom>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2023 gloriouscow.
 *          Copyright 2023 Miran Grca.
 */
#ifndef EMU_QUEUE_H
#define EMU_QUEUE_H

typedef enum queue_delay_t {
    DELAY_READ,
    DELAY_WRITE,
    DELAY_NONE
} queue_delay_t;

#define FLAG_PRELOADED 0x8000

extern void          queue_set_size(uintptr_t size);
extern uintptr_t     queue_get_len(void);
extern int           queue_is_full(void);
extern uint16_t      queue_get_preload(void);
extern int           queue_has_preload(void);
extern void          queue_set_preload(void);
extern void          queue_push8(uint8_t byte);
extern void          queue_push16(uint16_t word);
extern uint8_t       queue_pop(void);
extern queue_delay_t queue_get_delay(void);
extern void          queue_flush(void);

extern void          queue_init(void);

#endif /*EMU_QUEUE_H*/
