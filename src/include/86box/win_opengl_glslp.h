/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Header file for shader file parser.
 *
 * Authors:	Teemu Korhonen
 *
 *		Copyright 2021 Teemu Korhonen
 */

#ifndef WIN_OPENGL_GLSLP_H
#define WIN_OPENGL_GLSLP_H

#include <glad/glad.h>

GLuint load_custom_shaders(const char *path);
GLuint load_default_shaders();

#endif /*!WIN_OPENGL_GLSLP_H*/
