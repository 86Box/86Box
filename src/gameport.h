extern device_t gameport_device;
extern device_t gameport_201_device;

typedef struct
{
        char name[80];
        void *(*init)();
        void (*close)(void *p);
        uint8_t (*read)(void *p);
        void (*write)(void *p);
        int (*read_axis)(void *p, int axis);
        void (*a0_over)(void *p);
        int axis_count, button_count, pov_count;
        int max_joysticks;
        char axis_names[8][32];
        char button_names[32][32];
        char pov_names[4][32];
} joystick_if_t;

extern int joystick_type;
char *joystick_get_name(int joystick);
int joystick_get_max_joysticks(int joystick);
int joystick_get_axis_count(int joystick);
int joystick_get_button_count(int joystick);
int joystick_get_pov_count(int joystick);
char *joystick_get_axis_name(int joystick, int id);
char *joystick_get_button_name(int joystick, int id);
char *joystick_get_pov_name(int joystick, int id);

void gameport_update_joystick_type();

#define AXIS_NOT_PRESENT -99999
