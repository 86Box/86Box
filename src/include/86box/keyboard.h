/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the keyboard interface.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 */
#ifndef EMU_KEYBOARD_H
#define EMU_KEYBOARD_H

#define FLAG_AT        0x00  /* dev is AT         */
#define FLAG_PS2_KBD   0x10  /* dev is AT or PS/2 */
#define FLAG_AX        0x08  /* dev is AX         */
#define FLAG_TYPE_MASK 0x07  /* mask for type     */

enum {
    KBD_83_KEY = 0,
    KBD_84_KEY,
    KBD_101_KEY,
    KBD_102_KEY,
    KBD_JIS,
    KBD_KSC,
    KBD_ABNT2
};

enum {
    DEV_KBD = 0,
    DEV_AUX = 1
};

enum {
    DEV_STATE_MAIN_1                = 0,
    DEV_STATE_MAIN_OUT              = 1,
    DEV_STATE_MAIN_2                = 2,
    DEV_STATE_MAIN_CMD              = 3,
    DEV_STATE_MAIN_WANT_IN          = 4,
    DEV_STATE_MAIN_IN               = 5,
    DEV_STATE_EXECUTE_BAT           = 6,
    DEV_STATE_MAIN_WANT_EXECUTE_BAT = 7
};

enum {
    KEYBOARD_TYPE_INTERNAL          = 0,
    KEYBOARD_TYPE_PC_XT,
    KEYBOARD_TYPE_AT,
    KEYBOARD_TYPE_AX,
    KEYBOARD_TYPE_PS2,
    KEYBOARD_TYPE_PS55
};

/* Used by the AT / PS/2 keyboard controller, common device, keyboard, and mouse. */
typedef struct kbc_at_port_t {
    uint8_t wantcmd;
    uint8_t dat;

    int16_t out_new;

    void *priv;

    void (*poll)(void *priv);
} kbc_at_port_t;

/* Used by the AT / PS/2 common device, keyboard, and mouse. */
typedef struct atkbc_dev_t {
    const char *name; /* name of this device */

    uint8_t     type;
    uint8_t     command;
    uint8_t     last_scan_code;
    uint8_t     state;
    uint8_t     resolution;
    uint8_t     rate;
    uint8_t     cmd_queue_start;
    uint8_t     cmd_queue_end;
    uint8_t     queue_start;
    uint8_t     queue_end;

    uint16_t flags;

    /* Internal FIFO, not present on real devices, needed for commands that
       output multiple bytes. */
    uint8_t cmd_queue[16];

    uint8_t queue[64];

    int     fifo_mask;
    int     mode;
    int     x;
    int     y;
    int     z;
    int     b;
    int     ignore;

    int     *scan;

    void    (*process_cmd)(void *priv);
    void    (*execute_bat)(void *priv);

    kbc_at_port_t *port;
} atkbc_dev_t;

typedef struct scancode {
    const uint8_t mk[4];
    const uint8_t brk[4];
} scancode;

#define STATE_SHIFT_MASK         0x22
#define STATE_RSHIFT             0x20
#define STATE_LSHIFT             0x02

#define FAKE_LSHIFT_ON          0x100
#define FAKE_LSHIFT_OFF         0x101
#define LSHIFT_ON               0x102
#define LSHIFT_OFF              0x103
#define RSHIFT_ON               0x104
#define RSHIFT_OFF              0x105

#define KBC_VEN_GENERIC          0x00
#define KBC_VEN_ACER             0x01
#define KBC_VEN_ALI              0x02
#define KBC_VEN_AMI              0x03
#define KBC_VEN_AMI_TRIGEM       0x04
#define KBC_VEN_AWARD            0x05
#define KBC_VEN_CHIPS            0x06
#define KBC_VEN_COMPAQ           0x07
#define KBC_VEN_HOLTEK           0x08
#define KBC_VEN_IBM              0x09
#define KBC_VEN_NCR              0x0a
#define KBC_VEN_OLIVETTI         0x0b
#define KBC_VEN_QUADTEL          0x0c
#define KBC_VEN_PHOENIX          0x0d
#define KBC_VEN_SIEMENS          0x0e
#define KBC_VEN_TOSHIBA          0x0f
#define KBC_VEN_VIA              0x10
#define KBC_VEN_UMC              0x11
#define KBC_VEN_SIS              0x12
#define KBC_VEN_MASK             0x1f

#define KBC_FLAG_IS_ASIC   0x80000000
#define KBC_FLAG_IS_CLONE  0x40000000
#define KBC_FLAG_IS_GREEN  0x20000000
#define KBC_FLAG_IS_TYPE2  0x10000000

#ifdef __cplusplus
extern "C" {
#endif

extern int     keyboard_type;

extern uint8_t keyboard_mode;
extern int     keyboard_scan;

extern uint16_t scancode_map[768];
extern uint16_t scancode_config_map[768];

extern void (*keyboard_send)(uint16_t val);
extern void kbd_adddata_xt_common(uint16_t val);
extern void kbd_adddata_process(uint16_t val, void (*adddata)(uint16_t val));
extern void kbd_adddata_process_10x(uint16_t val, void (*adddata)(uint16_t val));

extern const scancode scancode_xt[512];
extern const scancode scancode_set8a[512];

extern uint8_t keyboard_set3_flags[512];
extern uint8_t keyboard_set3_all_repeat;
extern uint8_t keyboard_set3_all_break;
extern int     mouse_queue_start;
extern int     mouse_queue_end;
extern int     mouse_cmd_queue_start;
extern int     mouse_cmd_queue_end;
extern int     mouse_scan;

extern kbc_at_port_t     *kbc_at_ports[2];

#ifdef EMU_DEVICE_H
extern const device_t kbc_pc_device;
extern const device_t kbc_pc82_device;
extern const device_t kbc_pravetz_device;
extern const device_t kbc_xt_device;
extern const device_t kbc_xt86_device;
extern const device_t kbc_xt_compaq_device;
extern const device_t kbc_xt_t1x00_device;
extern const device_t kbc_tandy_device;
extern const device_t kbc_xt_lxt3_device;
extern const device_t kbc_xt_olivetti_device;
extern const device_t kbc_xt_zenith_device;
extern const device_t kbc_xt_hyundai_device;
extern const device_t kbc_xt_fe2010_device;
extern const device_t kbc_xtclone_device;

extern const device_t kbc_at_device;

extern const device_t keyboard_pc_xt_device;
extern const device_t keyboard_at_device;
extern const device_t keyboard_ax_device;
extern const device_t keyboard_ps2_device;
extern const device_t keyboard_ps55_device;
extern const device_t keyboard_at_generic_device;
#endif /*EMU_DEVICE_H*/

extern void     keyboard_init(void);
extern void     keyboard_close(void);
extern void     keyboard_set_table(const scancode *ptr);
extern void     keyboard_poll_host(void);
extern void     keyboard_process(void);
extern void     keyboard_process_10x(void);
extern uint16_t keyboard_convert(int ch);
extern void     keyboard_input(int down, uint16_t scan);
extern void     keyboard_all_up(void);
extern void     keyboard_update_states(uint8_t cl, uint8_t nl, uint8_t sl, uint8_t kl);
extern uint8_t  keyboard_get_shift(void);
extern void     keyboard_set_in_reset(uint8_t in_reset);
extern uint8_t  keyboard_get_in_reset(void);
extern void     keyboard_get_states(uint8_t *cl, uint8_t *nl, uint8_t *sl, uint8_t *kl);
extern void     keyboard_set_states(uint8_t cl, uint8_t nl, uint8_t sl);
extern int      keyboard_recv(uint16_t key);
extern int      keyboard_recv_ui(uint16_t key);
extern int      keyboard_isfsenter(void);
extern int      keyboard_isfsenter_up(void);
extern int      keyboard_isfsexit(void);
extern int      keyboard_isfsexit_up(void);
extern void     keyboard_set_is_amstrad(int ams);
extern void     kbc_at_set_ps2(void *priv, uint8_t ps2);
extern uint8_t  kbc_at_read_p(void *priv, uint8_t port, uint8_t mask);
extern void     kbc_at_write_p(void *priv, uint8_t port, uint8_t mask, uint8_t val);

extern void         kbc_at_set_fast_reset(uint8_t new_fast_reset);
extern void         kbc_at_port_handler(int num, int set, uint16_t port, void *priv);
extern void         kbc_at_handler(int set, uint16_t port, void *priv);
extern void         kbc_at_set_irq(int num, uint16_t irq, void *priv);

extern void         kbc_at_dev_queue_reset(atkbc_dev_t *dev, uint8_t reset_main);
extern uint8_t      kbc_at_dev_queue_pos(atkbc_dev_t *dev, uint8_t main);
extern void         kbc_at_dev_queue_add(atkbc_dev_t *dev, uint8_t val, uint8_t main);
extern void         kbc_at_dev_reset(atkbc_dev_t *dev, int do_fa);
extern atkbc_dev_t *kbc_at_dev_init(uint8_t inst);
/* This is so we can disambiguate scan codes that would otherwise conflict and get
   passed on incorrectly. */
extern uint16_t     convert_scan_code(uint16_t scan_code);

extern const char *    keyboard_get_name(int mouse);
extern const char *    keyboard_get_internal_name(int mouse);
extern int             keyboard_get_from_internal_name(char *s);
extern int             keyboard_has_config(int mouse);
#ifdef EMU_DEVICE_H
extern const device_t *keyboard_get_device(int mouse);
#endif
extern int             keyboard_get_ndev(void);
extern void            keyboard_add_device(void);

extern const scancode  scancode_set1[512];

#ifdef __cplusplus
}
#endif

#endif /*EMU_KEYBOARD_H*/
