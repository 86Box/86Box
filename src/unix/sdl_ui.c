#include <SDL.h>

#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/version.h>

#include "sdl_osd.h"

int
ui_msgbox(int flags, void *message)
{
    return ui_msgbox_header(flags, NULL, message);
}

int
ui_msgbox_header(int flags, void *header, void *message)
{
    SDL_MessageBoxData       msgdata;
    SDL_MessageBoxButtonData msgbtn;

    if (!header) {
        if (flags & MBX_ANSI)
            header = (void *) EMU_NAME;
        else
            header = (void *) EMU_NAME_W;
    }

    msgbtn.buttonid = 1;
    msgbtn.text     = "OK";
    msgbtn.flags    = 0;
    memset(&msgdata, 0, sizeof(SDL_MessageBoxData));
    msgdata.numbuttons = 1;
    msgdata.buttons    = &msgbtn;
    int msgflags       = 0;
    if (msgflags & MBX_FATAL)
        msgflags |= SDL_MESSAGEBOX_ERROR;
    else if (msgflags & MBX_ERROR || msgflags & MBX_WARNING)
        msgflags |= SDL_MESSAGEBOX_WARNING;
    else
        msgflags |= SDL_MESSAGEBOX_INFORMATION;
    msgdata.flags = msgflags;
    if (flags & MBX_ANSI) {
        int button      = 0;
        msgdata.title   = header;
        msgdata.message = message;
        SDL_ShowMessageBox(&msgdata, &button);
        return button;
    } else {
        int   button    = 0;
        char *res       = SDL_iconv_string("UTF-8", sizeof(wchar_t) == 2 ? "UTF-16LE" : "UTF-32LE", (char *) message, wcslen(message) * sizeof(wchar_t) + sizeof(wchar_t));
        char *res2      = SDL_iconv_string("UTF-8", sizeof(wchar_t) == 2 ? "UTF-16LE" : "UTF-32LE", (char *) header, wcslen(header) * sizeof(wchar_t) + sizeof(wchar_t));
        msgdata.message = res;
        msgdata.title   = res2;
        SDL_ShowMessageBox(&msgdata, &button);
        free(res);
        free(res2);
        return button;
    }

    return 0;
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
ui_sb_set_text_w(UNUSED(wchar_t *wstr))
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

void
ui_hard_reset_completed(void)
{
    /* No-op. */
}

void
ui_update_force_interpreter(void)
{
    /* No-op. */
}
