/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *
 *          Copyright 2021 Joakim L. Gilje
 */
/*
 * C functionality for Qt platform, where the C equivalent is not easily
 * implemented in Qt
 */
#if !defined(_WIN32) || !defined(__clang__)
#    include <strings.h>
#endif
#include <string.h>
#include <stdint.h>
#include <wchar.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/renderdefs.h>

int
qt_nvr_save(void)
{
    return nvr_save();
}

int
plat_vidapi(const char *api)
{
    if (!strcasecmp(api, RENDERER_NAME_DEFAULT) || !strcasecmp(api, RENDERER_NAME_SYSTEM))
        return RENDERER_SOFTWARE;
    else if (!strcasecmp(api, RENDERER_NAME_QT_SOFTWARE))
        return RENDERER_SOFTWARE;
    else if (!strcasecmp(api, RENDERER_NAME_QT_OPENGL) || !strcasecmp(api, RENDERER_NAME_QT_OPENGLES) || !strcasecmp(api, RENDERER_NAME_QT_OPENGL3))
        return RENDERER_OPENGL3;
    else if (!strcasecmp(api, RENDERER_NAME_QT_VULKAN))
        return RENDERER_VULKAN;
    else if (!strcasecmp(api, RENDERER_NAME_VNC))
        return RENDERER_VNC;

    return 0;
}

char *
plat_vidapi_name(int api)
{
    char *name = RENDERER_NAME_DEFAULT;

    switch (api) {
        case RENDERER_SOFTWARE:
            name = RENDERER_NAME_QT_SOFTWARE;
            break;
        case RENDERER_OPENGL3:
            name = RENDERER_NAME_QT_OPENGL3;
            break;
        case RENDERER_VULKAN:
            name = RENDERER_NAME_QT_VULKAN;
            break;
        case RENDERER_VNC:
            name = RENDERER_NAME_VNC;
            break;
        default:
            fatal("Unknown renderer: %i\n", api);
            break;
    }

    return name;
}
