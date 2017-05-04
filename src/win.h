/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
extern HINSTANCE hinstance;
extern HWND ghwnd;
extern int mousecapture;

#ifdef __cplusplus
extern "C" {
#endif

#define szClassName "86BoxMainWnd"
#define szSubClassName "86BoxSubWnd"
#define szStatusBarClassName "86BoxStatusBar"

void leave_fullscreen();

#ifdef __cplusplus
}
#endif


void status_open(HWND hwnd);
extern HWND status_hwnd;
extern int status_is_open;

void deviceconfig_open(HWND hwnd, struct device_t *device);
void joystickconfig_open(HWND hwnd, int joy_nr, int type);

extern char openfilestring[260];

int getfile(HWND hwnd, char *f, char *fn);
int getsfile(HWND hwnd, char *f, char *fn);

void get_executable_name(char *s, int size);
void set_window_title(char *s);

void startblit();
void endblit();

extern int pause;

void win_settings_open(HWND hwnd);
void win_menu_update();

void update_status_bar_panes(HWND hwnds);

int fdd_type_to_icon(int type);

extern HWND hwndStatus;
