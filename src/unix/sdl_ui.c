#ifdef USE_SDL2_LIB
#include <SDL.h>
#else
#include <SDL3/SDL.h>
#endif

#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/version.h>

#include "sdl_osd.h"

int
ui_msgbox(int flags, char *message)
{
    return ui_msgbox_header(flags, NULL, message);
}

int
ui_msgbox_header(int flags, char *header, char *message)
{
    SDL_MessageBoxData       msgdata;
    SDL_MessageBoxButtonData msgbtn;

    if (!header) {
        if (flags & MBX_FATAL)
            header = "Fatal error";
        else if (flags & MBX_ERROR)
            header = "Error";
        else
            header = EMU_NAME;
    }

#ifdef USE_SDL2_LIB
    msgbtn.buttonid = 1;
#else
    msgbtn.buttonID = 1;
#endif
    msgbtn.text     = "OK";
    msgbtn.flags    = 0;
    memset(&msgdata, 0, sizeof(SDL_MessageBoxData));
    msgdata.numbuttons = 1;
    msgdata.buttons    = &msgbtn;

    /* Yes returns 1, No 0, as the default. */
    SDL_MessageBoxButtonData yesno[2];
    if (flags & MBX_QUESTION_YN) {
        memset(yesno, 0, sizeof(yesno));
#ifdef USE_SDL2_LIB
        yesno[0].buttonid = 0;
        yesno[1].buttonid = 1;
#else
        yesno[0].buttonID = 0;
        yesno[1].buttonID = 1;
#endif
        yesno[0].text      = "No";
        yesno[0].flags     = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
        yesno[1].text      = "Yes";
        msgdata.numbuttons = 2;
        msgdata.buttons    = yesno;
    }
    int msgflags       = 0;
    if ((flags & MBX_ERROR) || (flags & MBX_FATAL))
        msgflags |= SDL_MESSAGEBOX_ERROR;
    else if (flags & MBX_WARNING)
        msgflags |= SDL_MESSAGEBOX_WARNING;
    else
        msgflags |= SDL_MESSAGEBOX_INFORMATION;
    msgdata.flags   = msgflags;
    int button      = 0;
    msgdata.title   = header;
    msgdata.message = message;
    SDL_ShowMessageBox(&msgdata, &button);
    return button;
}

void
ui_sb_update_icon_state(int tag, int state)
{
    osd_ui_sb_update_icon_state(tag, state);
}

void
ui_sb_update_icon(int tag, int active)
{
    osd_ui_sb_update_icon(tag, active);
}

void
ui_sb_update_icon_write(int tag, int active)
{
    osd_ui_sb_update_icon_write(tag, active);
}

void
ui_sb_update_icon_wp(int tag, int state)
{
    osd_ui_sb_update_icon_wp(tag, state);
}

void
ui_sb_update_tip(UNUSED(int arg))
{
    /* No-op. */
}

void
ui_sb_update_panes(void)
{
    /* No-op. */
}

void
ui_sb_update_text(void)
{
    /* No-op. */
}

void
ui_sb_set_text(UNUSED(char *wstr))
{
    /* No-op. */
}

void
ui_sb_bugui(UNUSED(char *str))
{
    /* No-op. */
}

void
ui_sb_set_ready(UNUSED(int ready))
{
    /* No-op. */
}

void
ui_sb_mt32lcd(UNUSED(char *str))
{
    /* No-op. */
}

extern void update_mouse_msg(void);
void
ui_hard_reset_completed(void)
{
    update_mouse_msg();
}

void
ui_update_force_interpreter(void)
{
    /* No-op. */
}
