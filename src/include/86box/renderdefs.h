/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          definitions for renderers
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025 Jasmine Iwanek.
 */
#ifndef EMU_RENDERDEFS_H
#define EMU_RENDERDEFS_H

#define RENDERER_NAME_DEFAULT     "default"
#define RENDERER_NAME_SYSTEM      "system"
#define RENDERER_NAME_QT_SOFTWARE "qt_software"
#define RENDERER_NAME_QT_OPENGL   "qt_opengl"
#define RENDERER_NAME_QT_OPENGLES "qt_opengles"
#define RENDERER_NAME_QT_OPENGL3  "qt_opengl3"
#define RENDERER_NAME_QT_VULKAN   "qt_vulkan"
#define RENDERER_NAME_VNC         "vnc"

#define RENDERER_SOFTWARE 0
#define RENDERER_OPENGL3  1
#define RENDERER_VULKAN   2
#define RENDERER_VNC      3

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#endif /*EMU_RENDERDEFS_H*/
