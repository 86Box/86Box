/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          FIFO infrastructure header.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2023-2025 Miran Grca.
 */
#ifndef FIFO_H
#define FIFO_H

#define FIFO(size)                      \
    typedef struct {                    \
        int     start;                  \
        int     end;                    \
        int     trigger_len;            \
        int     len;                    \
        int     empty;                  \
        int     overrun;                \
        int     full;                   \
        int     ready;                  \
        int     d_empty;                \
        int     d_overrun;              \
        int     d_full;                 \
        int     d_ready;                \
                                        \
        void   *priv;                   \
                                        \
        void  (*d_empty_evt)(void *);   \
        void  (*d_overrun_evt)(void *); \
        void  (*d_full_evt)(void *);    \
        void  (*d_ready_evt)(void *);   \
                                        \
        uint8_t tag[64];                \
        uint8_t buf[size];              \
    } fifo## size ##_t;

FIFO()

FIFO(16)
#define fifo16_init() fifo_init(16)

FIFO(64)
#define fifo64_init() fifo_init(64)

extern int        fifo_get_count(void *priv);
extern void       fifo_write(uint8_t val, void *priv);
extern void       fifo_write_tagged(uint8_t tag, uint8_t val, void *priv);
extern void       fifo_write_evt(uint8_t val, void *priv);
extern void       fifo_write_evt_tagged(uint8_t tag, uint8_t val, void *priv);
extern uint8_t    fifo_read(void *priv);
extern uint8_t    fifo_read_tagged(uint8_t *tag, void *priv);
extern uint8_t    fifo_read_evt(void *priv);
extern uint8_t    fifo_read_evt_tagged(uint8_t *tag, void *priv);
extern void       fifo_clear_overrun(void *priv);
extern int        fifo_get_full(void *priv);
extern int        fifo_get_d_full(void *priv);
extern int        fifo_get_empty(void *priv);
extern int        fifo_get_d_empty(void *priv);
extern int        fifo_get_overrun(void *priv);
extern int        fifo_get_d_overrun(void *priv);
extern int        fifo_get_ready(void *priv);
extern int        fifo_get_d_ready(void *priv);
extern int        fifo_get_trigger_len(void *priv);
extern void       fifo_set_trigger_len(void *priv, int trigger_len);
extern void       fifo_set_len(void *priv, int len);
extern void       fifo_set_d_full_evt(void *priv, void (*d_full_evt)(void *));
extern void       fifo_set_d_empty_evt(void *priv, void (*d_empty_evt)(void *));
extern void       fifo_set_d_overrun_evt(void *priv, void (*d_overrun_evt)(void *));
extern void       fifo_set_d_ready_evt(void *priv, void (*d_ready_evt)(void *));
extern void       fifo_set_priv(void *priv, void *sub_priv);
extern void       fifo_reset(void *priv);
extern void       fifo_reset_evt(void *priv);
extern void       fifo_close(void *priv);
extern void      *fifo_init(int len);

#endif /*FIFO_H*/
