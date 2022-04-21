/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *
 *
 * Authors:	Joakim L. Gilje <jgilje@jgilje.net>
 *
 *		Copyright 2021 Joakim L. Gilje
 */
/*
 * C functionality for Qt platform, where the C equivalent is not easily
 * implemented in Qt
 */
#if !defined(_WIN32) || !defined(__clang__)
#include <strings.h>
#endif
#include <string.h>
#include <stdint.h>
#include <wchar.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/timer.h>
#include <86box/nvr.h>

int qt_nvr_save(void) {
    return nvr_save();
}

char  icon_set[256] = "";  /* name of the iconset to be used */

int
plat_vidapi(char* api) {
    if (!strcasecmp(api, "default") || !strcasecmp(api, "system")) {
        return 0;
    } else if (!strcasecmp(api, "qt_software")) {
        return 0;
    } else if (!strcasecmp(api, "qt_opengl")) {
        return 1;
    } else if (!strcasecmp(api, "qt_opengles")) {
        return 2;
    } else if (!strcasecmp(api, "qt_opengl3")) {
        return 3;
    } else if (!strcasecmp(api, "qt_vulkan")) {
        return 4;
    }

    return 0;
}

char* plat_vidapi_name(int api) {
    char* name = "default";

    switch (api) {
    case 0:
        name = "qt_software";
        break;
    case 1:
        name = "qt_opengl";
        break;
    case 2:
        name = "qt_opengles";
        break;
    case 3:
        name = "qt_opengl3";
        break;
    case 4:
        name = "qt_vulkan";
        break;
    default:
        fatal("Unknown renderer: %i\n", api);
        break;
    }

    return name;
}
