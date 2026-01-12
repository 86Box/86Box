/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the generic game port handlers.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Sarah Walker, <https://pcem-emulator.co.uk/>
 *          RichardG, <richardg867@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2016-2022 Miran Grca.
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2021      RichardG.
 *          Copyright 2021-2025 Jasmine Iwanek.
 */
#ifndef EMU_GAMEPORT_H
#define EMU_GAMEPORT_H

#define GAMEPORT_MAX 2

#define MAX_PLAT_JOYSTICKS  8
#define MAX_JOYSTICKS       4

#define MAX_JOY_AXES    16
#define MAX_JOY_BUTTONS 32
#define MAX_JOY_POVS    4

#define JS_TYPE_NONE               0
#define JS_TYPE_2AXIS_4BUTTON      1
#define JS_TYPE_2AXIS_6BUTTON      2
#define JS_TYPE_2AXIS_8BUTTON      3
#define JS_TYPE_4AXIS_4BUTTON      4
#define JS_TYPE_CH_FLIGHTSTICK_PRO 5
#define JS_TYPE_SIDEWINDER_PAD     6
#define JS_TYPE_THRUSTMASTER_FCS   7


#define POV_X               0x80000000
#define POV_Y               0x40000000

#define AXIS_NOT_PRESENT    -99999

#define JOYSTICK_PRESENT(gp, js) (joystick_state[gp][js].plat_joystick_nr != 0)

#define GAMEPORT_1ADDR      0x010000
#define GAMEPORT_6ADDR      0x060000
#define GAMEPORT_8ADDR      0x080000
#define GAMEPORT_SIO        0x1000000
#define GAMEPORT_PNPROM     0x2000000

typedef struct joystick_t {
    const char *name;
    const char *internal_name;

    void   *(*init)(void);
    void    (*close)(void *priv);
    uint8_t (*read)(void *priv);
    void    (*write)(void *priv);
    int     (*read_axis)(void *priv, int axis);
    void    (*a0_over)(void *priv);

    int         axis_count;
    int         button_count;
    int         pov_count;
    int         max_joysticks;
    const char *axis_names[MAX_JOY_AXES];
    const char *button_names[MAX_JOY_BUTTONS];
    const char *pov_names[MAX_JOY_POVS];
} joystick_t;

typedef struct plat_joystick_state_t {
    char name[260];

    int a[MAX_JOY_AXES];
    int b[MAX_JOY_BUTTONS];
    int p[MAX_JOY_POVS];

    struct {
        char name[260];
        int  id;
    } axis[MAX_JOY_AXES];

    struct {
        char name[260];
        int  id;
    } button[MAX_JOY_BUTTONS];

    struct {
        char name[260];
        int  id;
    } pov[MAX_JOY_POVS];

    int nr_axes;
    int nr_buttons;
    int nr_povs;
} plat_joystick_state_t;

typedef struct joystick_state_t {
    int axis[MAX_JOY_AXES];
    int button[MAX_JOY_BUTTONS];
    int pov[MAX_JOY_POVS];

    int plat_joystick_nr;
    int axis_mapping[MAX_JOY_AXES];
    int button_mapping[MAX_JOY_BUTTONS];
    int pov_mapping[MAX_JOY_POVS][2];
} joystick_state_t;

extern device_t game_ports[GAMEPORT_MAX];

#ifdef __cplusplus
extern "C" {
#endif

extern int gameport_available(int port);
#ifdef EMU_DEVICE_H
extern const device_t *gameport_get_device(int port);
#endif
extern int         gameport_has_config(int port);
extern const char *gameport_get_internal_name(int port);
extern int         gameport_get_from_internal_name(const char *str);

#ifdef EMU_DEVICE_H
extern const device_t gameport_device;
extern const device_t gameport_200_device;
extern const device_t gameport_201_device;
extern const device_t gameport_203_device;
extern const device_t gameport_205_device;
extern const device_t gameport_207_device;
extern const device_t gameport_208_device;
extern const device_t gameport_209_device;
extern const device_t gameport_20b_device;
extern const device_t gameport_20d_device;
extern const device_t gameport_20f_device;
extern const device_t gameport_tm_acm_device;
extern const device_t gameport_pnp_device;
extern const device_t gameport_pnp_1io_device;
extern const device_t gameport_pnp_6io_device;
extern const device_t gameport_sio_device;
extern const device_t gameport_sio_1io_device;

extern const device_t *standalone_gameport_type;
#endif
extern int                   gameport_instance_id;
extern plat_joystick_state_t plat_joystick_state[MAX_PLAT_JOYSTICKS];
extern joystick_state_t      joystick_state[GAMEPORT_MAX][MAX_JOYSTICKS];
extern int                   joysticks_present;

extern int joystick_type[GAMEPORT_MAX];

extern void joystick_init(void);
extern void joystick_close(void);
extern void joystick_process(uint8_t gp);

extern const char *joystick_get_name(int js);
extern const char *joystick_get_internal_name(int js);
extern int         joystick_get_from_internal_name(char *s);
extern int         joystick_get_max_joysticks(int js);
extern int         joystick_get_axis_count(int js);
extern int         joystick_get_button_count(int js);
extern int         joystick_get_pov_count(int js);
extern const char *joystick_get_axis_name(int js, int id);
extern const char *joystick_get_button_name(int js, int id);
extern const char *joystick_get_pov_name(int js, int id);

extern void  gameport_update_joystick_type(uint8_t gp);
extern void  gameport_remap(void *priv, uint16_t address);
extern void *gameport_add(const device_t *gameport_type);

// Paddle Controllers
extern const joystick_t joystick_generic_paddle;

// 2 axis Generic Joysticks
extern const joystick_t joystick_2axis_1button;
extern const joystick_t joystick_2axis_2button;
extern const joystick_t joystick_2axis_3button;
extern const joystick_t joystick_2axis_4button;
extern const joystick_t joystick_2axis_6button;
extern const joystick_t joystick_2axis_8button;

// 3 axis Generic Joysticks
extern const joystick_t joystick_3axis_2button;
extern const joystick_t joystick_3axis_3button;
extern const joystick_t joystick_3axis_4button;

// 4 axis Generic Joysticks
extern const joystick_t joystick_4axis_2button;
extern const joystick_t joystick_4axis_3button;
extern const joystick_t joystick_4axis_4button;

// Generic Gamepads
extern const joystick_t joystick_2button_gamepad;
extern const joystick_t joystick_3button_gamepad;
extern const joystick_t joystick_4button_gamepad;
extern const joystick_t joystick_6button_gamepad;

extern const joystick_t joystick_gravis_gamepad;

// Generic Steering Wheels
extern const joystick_t joystick_steering_wheel_2_button;
extern const joystick_t joystick_steering_wheel_3_button;
extern const joystick_t joystick_steering_wheel_4_button;

// Generic Flight Yokes
extern const joystick_t joystick_2button_flight_yoke;
extern const joystick_t joystick_4button_flight_yoke;
extern const joystick_t joystick_3button_flight_yoke;

extern const joystick_t joystick_2button_yoke_throttle;
extern const joystick_t joystick_3button_yoke_throttle;
extern const joystick_t joystick_4button_yoke_throttle;

extern const joystick_t joystick_ch_flightstick;
extern const joystick_t joystick_ch_flightstick_ch_pedals;
extern const joystick_t joystick_ch_flightstick_ch_pedals_pro;

extern const joystick_t joystick_ch_flightstick_pro;
extern const joystick_t joystick_ch_flightstick_pro_ch_pedals;
extern const joystick_t joystick_ch_flightstick_pro_ch_pedals_pro;

extern const joystick_t joystick_ch_virtual_pilot;
extern const joystick_t joystick_ch_virtual_pilot_ch_pedals;
extern const joystick_t joystick_ch_virtual_pilot_ch_pedals_pro;

extern const joystick_t joystick_ch_virtual_pilot_pro;
extern const joystick_t joystick_ch_virtual_pilot_pro_ch_pedals;
extern const joystick_t joystick_ch_virtual_pilot_pro_ch_pedals_pro;

extern const joystick_t joystick_sw_pad;

extern const joystick_t joystick_tm_fcs;
extern const joystick_t joystick_tm_fcs_rcs;

extern const joystick_t joystick_tm_formula_t1t2;
extern const joystick_t joystick_tm_formula_t1t2wa;

#ifdef __cplusplus
}
#endif

#endif /*EMU_GAMEPORT_H*/
