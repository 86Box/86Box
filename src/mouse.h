void mouse_emu_init();
void mouse_emu_close();
void mouse_poll(int x, int y, int z, int b);

char *mouse_get_name(int mouse);
int mouse_get_type(int mouse);

#define MOUSE_TYPE_SERIAL  0
#define MOUSE_TYPE_PS2     1
#define MOUSE_TYPE_AMSTRAD 2
#define MOUSE_TYPE_OLIM24  3

#define MOUSE_TYPE_IF_MASK 3

#define MOUSE_TYPE_3BUTTON (1 << 31)

typedef struct
{
        char name[80];
        void *(*init)();
        void (*close)(void *p);
        uint8_t (*poll)(int x, int y, int z, int b, void *p);
        int type;
} mouse_t;

extern int mouse_type;
