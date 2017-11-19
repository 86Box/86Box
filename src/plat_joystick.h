#define MAX_PLAT_JOYSTICKS	8
#define MAX_JOYSTICKS		4

#define POV_X	0x80000000
#define POV_Y	0x40000000


typedef struct {
    char	name[64];

    int		a[8];
    int		b[32];
    int		p[4];

    struct {
	char name[32];
	int id;
    }		axis[8];

    struct {
	char name[32];
	int id;
    }		button[32];

    struct {
	char name[32];
	int id;
    }		pov[4];

    int		nr_axes;
    int		nr_buttons;
    int		nr_povs;
} plat_joystick_t;

typedef struct {
    int		axis[8];
    int		button[32];
    int		pov[4];

    int		plat_joystick_nr;
    int		axis_mapping[8];
    int		button_mapping[32];
    int		pov_mapping[4][2];
} joystick_t;


#ifdef __cplusplus
extern "C" {
#endif

extern plat_joystick_t	plat_joystick_state[MAX_PLAT_JOYSTICKS];
extern joystick_t	joystick_state[MAX_JOYSTICKS];
extern int		joysticks_present;


#define JOYSTICK_PRESENT(n) (joystick_state[n].plat_joystick_nr != 0)


extern void	joystick_init(void);
extern void	joystick_close(void);
extern void	joystick_process(void);

#ifdef __cplusplus
}
#endif
