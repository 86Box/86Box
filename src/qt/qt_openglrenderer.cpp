/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          OpenGL renderer for Qt, mostly ported over from PCem.
 *
 * Authors: Teemu Korhonen
 *          Cacodemon345
 *          bit
 *          Sarah Walker
 *
 *          Copyright 2022 Teemu Korhonen
 *          Copyright 2025 Cacodemon345
 *          Copyright 2017 Bit
 *          Copyright 2017-2020 Sarah Walker
 */
#include "qt_renderercommon.hpp"
#include "qt_mainwindow.hpp"

extern MainWindow *main_window;

#include <QCoreApplication>
#include <QMessageBox>
#include <QWindow>
#include <QClipboard>
#include <QPainter>
#include <QWidget>
#include <QEvent>
#include <QApplication>
#include <QString>
#include <QByteArray>

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QOpenGLTexture>
#include <QOpenGLDebugLogger>

#include <QImage>

#include <cmath>
#include <cstdarg>
#define HAVE_STDARG_H

#include "qt_openglrenderer.hpp"
#include "qt_openglshadermanagerdialog.hpp"

extern "C" {
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/video.h>
#include <86box/path.h>
#include <86box/ini.h>
#include <86box/config.h>
#include <86box/qt-glslp-parser.h>

char        gl3_shader_file[MAX_USER_SHADERS][512];
extern bool cpu_thread_running;
}

#define SCALE_SOURCE   0
#define SCALE_VIEWPORT 1
#define SCALE_ABSOLUTE 2

static GLfloat matrix[] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

extern int video_filter_method;
extern int video_vsync;
extern int video_focus_dim;
extern int video_refresh_rate;

const char *vertex_shader_default_tex_src =
#ifdef __APPLE__
    "#version 150\n"
#else
    "#version 130\n"
#endif
    "\n"
    "in vec4 VertexCoord;\n"
    "in vec2 TexCoord;\n"
    "\n"
    "out vec2 texCoord;\n"
    "\n"
    "void main()\n"
    "{\n"
    "       gl_Position = VertexCoord;\n"
    "       texCoord = TexCoord;\n"
    "}\n";

const char *fragment_shader_default_tex_src =
#ifdef __APPLE__
    "#version 150\n"
#else
    "#version 130\n"
#endif
    "\n"
    "in vec2 texCoord;\n"
    "uniform sampler2D Texture;\n"
    "\n"
    "out vec4 color;"
    "\n"
    "void main()\n"
    "{\n"
    "       color = texture(Texture, texCoord);\n"
    "       color.a = 1.0;\n"
    "}\n";

const char *vertex_shader_default_color_src =
#ifdef __APPLE__
    "#version 150\n"
#else
    "#version 130\n"
#endif
    "\n"
    "in vec4 VertexCoord;\n"
    "in vec4 Color;\n"
    "\n"
    "out vec4 color;\n"
    "\n"
    "void main()\n"
    "{\n"
    "       gl_Position = VertexCoord;\n"
    "       color = Color;\n"
    "}\n";

const char *fragment_shader_default_color_src =
#ifdef __APPLE__
    "#version 150\n"
#else
    "#version 130\n"
#endif
    "\n"
    "in vec4 color;\n"
    "\n"
    "out vec4 outColor;"
    "\n"
    "void main()\n"
    "{\n"
    "       outColor = color;\n"
    "       outColor.a = 1.0;\n"
    "}\n";

#ifdef ENABLE_OGL3_LOG
int ogl3_do_log = ENABLE_OGL3_LOG;

static void
ogl3_log(const char *fmt, ...)
{
    va_list ap;

    if (ogl3_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ogl3_log(fmt, ...)
#endif

static inline int
next_pow2(unsigned int n)
{
    n--;
    n |= n >> 1; // Divide by 2^k for consecutive doublings of k up to 32,
    n |= n >> 2; // and then or the results.
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;

    return n;
}

int
OpenGLRenderer::create_program(struct shader_program *program)
{
    GLint status;
    program->id = glw.glCreateProgram();
    glw.glAttachShader(program->id, program->vertex_shader);
    glw.glAttachShader(program->id, program->fragment_shader);

    glw.glLinkProgram(program->id);

    glw.glDeleteShader(program->vertex_shader);
    glw.glDeleteShader(program->fragment_shader);

    program->vertex_shader = program->fragment_shader = 0;

    glw.glGetProgramiv(program->id, GL_LINK_STATUS, &status);

    if (!status) {
        int maxLength;
        int length;
        glw.glGetProgramiv(program->id, GL_INFO_LOG_LENGTH, &maxLength);
        char *log = (char *) malloc(maxLength);
        glw.glGetProgramInfoLog(program->id, maxLength, &length, log);
        main_window->showMessage(MBX_ERROR | MBX_FATAL, tr("GLSL Error"), tr("Program not linked:\n\n%1").arg(log), false);
        // wx_simple_messagebox("GLSL Error", "Program not linked:\n%s", log);
        free(log);
        return 0;
    }

    return 1;
}

int
OpenGLRenderer::compile_shader(GLenum shader_type, const char *prepend, const char *program, int *dst)
{
    QRegularExpression versionRegex("^\\s*(#version\\s+\\w+)", QRegularExpression::MultilineOption);
    QString            progSource  = QString(program);
    QByteArray         finalSource = nullptr;
    const char        *source[5];
    char               version[50];
    char              *version_loc = (char *) strstr(program, "#version");
    if (version_loc) {
        snprintf(version, 49, "%s\n", versionRegex.match(progSource).captured(1).toLatin1().data());
        progSource.remove(versionRegex);
    } else {
        int ver = gl_version[0] * 100 + gl_version[1] * 10;
        if (ver == 300)
            ver = 130;
        else if (ver == 310)
            ver = 140;
        else if (ver == 320)
            ver = 150;
        snprintf(version, 49, "#version %d\n", ver);
    }

    /* Remove parameter lines. */
    progSource.remove(QRegularExpression("^\\s*#pragma parameter.*?\\n", QRegularExpression::MultilineOption));

    finalSource = progSource.toLatin1();

    source[0] = version;
    source[1] = "\n#extension GL_ARB_shading_language_420pack : enable\n";
    source[2] = prepend ? prepend : "";
    source[3] = "\n#line 1\n";
    source[4] = finalSource.data();

    GLuint shader = glw.glCreateShader(shader_type);
    glw.glShaderSource(shader, 5, source, NULL);
    glw.glCompileShader(shader);

    GLint status = 0;
    glw.glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        GLint length;
        glw.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        char *log = (char *) malloc(length);
        glw.glGetShaderInfoLog(shader, length, &length, log);
        main_window->showMessage(MBX_ERROR | MBX_FATAL, tr("GLSL Error"), tr("Could not compile shader:\n\n%1").arg(log), false);
        // wx_simple_messagebox("GLSL Error", "Could not compile shader:\n%s", log);

        ogl3_log("Could not compile shader: %s\n", log);
        //                ogl3_log("Shader: %s\n", program);

        free(log);
        return 0;
    }

    *dst = shader;

    return 1;
}

GLuint
OpenGLRenderer::get_uniform(GLuint program, const char *name)
{
    return glw.glGetUniformLocation(program, name);
}

GLuint
OpenGLRenderer::get_attrib(GLuint program, const char *name)
{
    return glw.glGetAttribLocation(program, name);
}

void
OpenGLRenderer::find_uniforms(struct glsl_shader *glsl, int num_pass)
{
    int                 i;
    char                s[50];
    struct shader_pass *pass = &glsl->passes[num_pass];
    int                 p    = pass->program.id;
    glw.glUseProgram(p);

    struct shader_uniforms *u = &pass->uniforms;

    u->mvp_matrix   = get_uniform(p, "MVPMatrix");
    u->vertex_coord = get_attrib(p, "VertexCoord");
    u->tex_coord    = get_attrib(p, "TexCoord");
    u->color        = get_attrib(p, "Color");

    u->frame_count     = get_uniform(p, "FrameCount");
    u->frame_direction = get_uniform(p, "FrameDirection");

    u->texture      = get_uniform(p, "Texture");
    u->input_size   = get_uniform(p, "InputSize");
    u->texture_size = get_uniform(p, "TextureSize");
    u->output_size  = get_uniform(p, "OutputSize");

    u->orig.texture      = get_uniform(p, "OrigTexture");
    u->orig.input_size   = get_uniform(p, "OrigInputSize");
    u->orig.texture_size = get_uniform(p, "OrigTextureSize");

    for (i = 0; i < glsl->num_passes; ++i) {
        snprintf(s, sizeof(s) - 1, "Pass%dTexture", (i + 1));
        u->pass[i].texture = get_uniform(p, s);
        snprintf(s, sizeof(s) - 1, "Pass%dInputSize", (i + 1));
        u->pass[i].input_size = get_uniform(p, s);
        snprintf(s, sizeof(s) - 1, "Pass%dTextureSize", (i + 1));
        u->pass[i].texture_size = get_uniform(p, s);

        snprintf(s, sizeof(s) - 1, "PassPrev%dTexture", num_pass - i);
        u->prev_pass[i].texture = get_uniform(p, s);
        snprintf(s, sizeof(s) - 1, "PassPrev%dInputSize", num_pass - i);
        u->prev_pass[i].input_size = get_uniform(p, s);
        snprintf(s, sizeof(s) - 1, "PassPrev%dTextureSize", num_pass - i);
        u->prev_pass[i].texture_size = get_uniform(p, s);
    }

    u->prev[0].texture   = get_uniform(p, "PrevTexture");
    u->prev[0].tex_coord = get_attrib(p, "PrevTexCoord");
    for (i = 1; i < MAX_PREV; ++i) {
        snprintf(s, sizeof(s) - 1, "Prev%dTexture", i);
        u->prev[i].texture = get_uniform(p, s);
        snprintf(s, sizeof(s) - 1, "Prev%dTexCoord", i);
        u->prev[i].tex_coord = get_attrib(p, s);
    }
    for (i = 0; i < MAX_PREV; ++i)
        if (u->prev[i].texture >= 0)
            glsl->has_prev = 1;

    for (i = 0; i < glsl->num_lut_textures; ++i)
        u->lut_textures[i] = get_uniform(p, glsl->lut_textures[i].name);

    for (i = 0; i < glsl->num_parameters; ++i)
        u->parameters[i] = get_uniform(p, glsl->parameters[i].id);

    glw.glUseProgram(0);
}

static void
set_scale_mode(char *scale, int *dst)
{
    if (!strcmp(scale, "viewport"))
        *dst = SCALE_VIEWPORT;
    else if (!strcmp(scale, "absolute"))
        *dst = SCALE_ABSOLUTE;
    else
        *dst = SCALE_SOURCE;
}

static void
setup_scale(struct shader *shader, struct shader_pass *pass)
{
    set_scale_mode(shader->scale_type_x, &pass->scale.mode[0]);
    set_scale_mode(shader->scale_type_y, &pass->scale.mode[1]);
    pass->scale.value[0] = shader->scale_x;
    pass->scale.value[1] = shader->scale_y;
}

void
OpenGLRenderer::create_texture(struct shader_texture *tex)
{
    if (tex->width > max_texture_size)
        tex->width = max_texture_size;
    if (tex->height > max_texture_size)
        tex->height = max_texture_size;
    ogl3_log("Create texture with size %dx%d\n", tex->width, tex->height);
    glw.glGenTextures(1, (GLuint *) &tex->id);
    glw.glBindTexture(GL_TEXTURE_2D, tex->id);
    glw.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, tex->wrap_mode);
    glw.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tex->wrap_mode);
    glw.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, tex->min_filter);
    glw.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, tex->mag_filter);
    glw.glTexImage2D(GL_TEXTURE_2D, 0, tex->internal_format, tex->width, tex->height, 0, tex->format, tex->type, tex->data);
    if (tex->mipmap)
        glw.glGenerateMipmap(GL_TEXTURE_2D);
    glw.glBindTexture(GL_TEXTURE_2D, 0);
}

void
OpenGLRenderer::delete_texture(struct shader_texture *tex)
{
    if (tex->id > 0)
        glw.glDeleteTextures(1, (GLuint *) &tex->id);
    tex->id = 0;
}

void
OpenGLRenderer::delete_fbo(struct shader_fbo *fbo)
{
    if (fbo->id >= 0) {
        glw.glDeleteFramebuffers(1, (GLuint *) &fbo->id);
        delete_texture(&fbo->texture);
    }
}

void
OpenGLRenderer::delete_program(struct shader_program *program)
{
    if (program->vertex_shader)
        glw.glDeleteShader(program->vertex_shader);
    if (program->fragment_shader)
        glw.glDeleteShader(program->fragment_shader);
    glw.glDeleteProgram(program->id);
}

void
OpenGLRenderer::delete_vbo(struct shader_vbo *vbo)
{
    if (vbo->color >= 0)
        glw.glDeleteBuffers(1, (GLuint *) &vbo->color);
    glw.glDeleteBuffers(1, (GLuint *) &vbo->vertex_coord);
    glw.glDeleteBuffers(1, (GLuint *) &vbo->tex_coord);
}

void
OpenGLRenderer::delete_pass(struct shader_pass *pass)
{
    delete_fbo(&pass->fbo);
    delete_vbo(&pass->vbo);
    delete_program(&pass->program);
    glw.glDeleteVertexArrays(1, (GLuint *) &pass->vertex_array);
}

void
OpenGLRenderer::delete_prev(struct shader_prev *prev)
{
    delete_fbo(&prev->fbo);
    delete_vbo(&prev->vbo);
}

void
OpenGLRenderer::delete_shader(struct glsl_shader *glsl)
{
    for (int i = 0; i < glsl->num_passes; ++i)
        delete_pass(&glsl->passes[i]);
    if (glsl->has_prev) {
        delete_pass(&glsl->prev_scene);
        for (int i = 0; i < MAX_PREV; ++i)
            delete_prev(&glsl->prev[i]);
    }
    for (int i = 0; i < glsl->num_lut_textures; ++i)
        delete_texture(&glsl->lut_textures[i].texture);
}

void
OpenGLRenderer::delete_glsl(glsl_t *glsl)
{
    for (int i = 0; i < glsl->num_shaders; ++i)
        delete_shader(&glsl->shaders[i]);
    delete_pass(&glsl->scene);
    delete_pass(&glsl->fs_color);
    delete_pass(&glsl->final_pass);
#ifdef SDL2_SHADER_DEBUG
    delete_pass(&glsl->debug);
#endif
}

void
OpenGLRenderer::create_fbo(struct shader_fbo *fbo)
{
    create_texture(&fbo->texture);

    glw.glGenFramebuffers(1, (GLuint *) &fbo->id);
    glw.glBindFramebuffer(GL_FRAMEBUFFER, fbo->id);
    glw.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo->texture.id, 0);

    if (glw.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        ogl3_log("Could not create framebuffer!\n");

    glw.glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void
OpenGLRenderer::setup_fbo(struct shader *shader, struct shader_fbo *fbo)
{
    fbo->texture.internal_format = GL_RGB8;
    fbo->texture.format          = GL_RGB;
    fbo->texture.min_filter = fbo->texture.mag_filter = shader->filter_linear ? GL_LINEAR : GL_NEAREST;
    fbo->texture.width                                = 2048;
    fbo->texture.height                               = 2048;
    fbo->texture.type                                 = GL_UNSIGNED_BYTE;
    if (!strcmp(shader->wrap_mode, "repeat"))
        fbo->texture.wrap_mode = GL_REPEAT;
    else if (!strcmp(shader->wrap_mode, "mirrored_repeat"))
        fbo->texture.wrap_mode = GL_MIRRORED_REPEAT;
    else if (!strcmp(shader->wrap_mode, "clamp_to_edge"))
        fbo->texture.wrap_mode = GL_CLAMP_TO_EDGE;
    else
        fbo->texture.wrap_mode = GL_CLAMP_TO_BORDER;
    fbo->srgb = 0;
    if (shader->srgb_framebuffer) {
        fbo->texture.internal_format = GL_SRGB8;
        fbo->srgb                    = 1;
    } else if (shader->float_framebuffer) {
        fbo->texture.internal_format = GL_RGB32F;
        fbo->texture.type            = GL_FLOAT;
    }

    if (fbo->texture.mipmap)
        fbo->texture.min_filter = shader->filter_linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;

    create_fbo(fbo);
}

void
OpenGLRenderer::recreate_fbo(struct shader_fbo *fbo, int width, int height)
{
    if (width != fbo->texture.width || height != fbo->texture.height) {
        glw.glDeleteFramebuffers(1, (GLuint *) &fbo->id);
        glw.glDeleteTextures(1, (GLuint *) &fbo->texture.id);
        fbo->texture.width  = width;
        fbo->texture.height = height;
        create_fbo(fbo);
    }
}

int
OpenGLRenderer::create_default_shader_tex(struct shader_pass *pass)
{
    if (!compile_shader(GL_VERTEX_SHADER, 0, vertex_shader_default_tex_src, &pass->program.vertex_shader) || !compile_shader(GL_FRAGMENT_SHADER, 0, fragment_shader_default_tex_src, &pass->program.fragment_shader) || !create_program(&pass->program))
        return 0;
    glw.glGenVertexArrays(1, (GLuint *) &pass->vertex_array);

    struct shader_uniforms *u = &pass->uniforms;
    int                     p = pass->program.id;
    memset(u, -1, sizeof(struct shader_uniforms));
    u->vertex_coord     = get_attrib(p, "VertexCoord");
    u->tex_coord        = get_attrib(p, "TexCoord");
    u->texture          = get_uniform(p, "Texture");
    pass->scale.mode[0] = pass->scale.mode[1] = SCALE_SOURCE;
    pass->scale.value[0] = pass->scale.value[1] = 1.0f;
    pass->fbo.id                                = -1;
    pass->active                                = 1;
    return 1;
}

int
OpenGLRenderer::create_default_shader_color(struct shader_pass *pass)
{
    if (!compile_shader(GL_VERTEX_SHADER, 0, vertex_shader_default_color_src, &pass->program.vertex_shader) || !compile_shader(GL_FRAGMENT_SHADER, 0, fragment_shader_default_color_src, &pass->program.fragment_shader) || !create_program(&pass->program))
        return 0;
    glw.glGenVertexArrays(1, (GLuint *) &pass->vertex_array);

    struct shader_uniforms *u = &pass->uniforms;
    int                     p = pass->program.id;
    memset(u, -1, sizeof(struct shader_uniforms));
    u->vertex_coord     = get_attrib(p, "VertexCoord");
    u->color            = get_attrib(p, "Color");
    pass->scale.mode[0] = pass->scale.mode[1] = SCALE_SOURCE;
    pass->scale.value[0] = pass->scale.value[1] = 1.0f;
    pass->fbo.id                                = -1;
    pass->active                                = 1;
    return 1;
}

/* create the default scene shader */
void
OpenGLRenderer::create_scene_shader()
{
    struct shader scene_shader_conf;
    memset(&scene_shader_conf, 0, sizeof(struct shader));
    create_default_shader_tex(&active_shader->scene);
    scene_shader_conf.filter_linear = video_filter_method;
    if (active_shader->num_shaders > 0 && active_shader->shaders[0].input_filter_linear >= 0)
        scene_shader_conf.filter_linear = active_shader->shaders[0].input_filter_linear;
    setup_fbo(&scene_shader_conf, &active_shader->scene.fbo);

    memset(&scene_shader_conf, 0, sizeof(struct shader));
    create_default_shader_color(&active_shader->fs_color);
    setup_fbo(&scene_shader_conf, &active_shader->fs_color.fbo);
}

static int
load_texture(const char *f, struct shader_texture *tex)
{
    QImage img;
    if (!img.load(f))
        return 0;
    int width, height;
    width  = img.size().width();
    height = img.size().height();

    if (width != next_pow2(width) || height != next_pow2(height))
        img = img.scaled(next_pow2(width), next_pow2(height));

    width  = img.size().width();
    height = img.size().height();

    img.convertTo(QImage::Format_RGB888);

    const GLubyte *rgb = img.constBits();

    int bpp = 3;

    GLubyte *data = (GLubyte *) malloc((size_t) width * height * bpp);

    int x, y, Y;
    for (y = 0; y < height; ++y) {
        Y = height - y - 1;
        for (x = 0; x < width; x++) {
            data[(y * width + x) * bpp + 0] = rgb[(Y * width + x) * 3 + 0];
            data[(y * width + x) * bpp + 1] = rgb[(Y * width + x) * 3 + 1];
            data[(y * width + x) * bpp + 2] = rgb[(Y * width + x) * 3 + 2];
        }
    }

    tex->width           = width;
    tex->height          = height;
    tex->internal_format = GL_RGB8;
    tex->format          = GL_RGB;
    tex->type            = GL_UNSIGNED_BYTE;
    tex->data            = data;
    return 1;
}

glsl_t *
OpenGLRenderer::load_glslp(glsl_t *glsl, int num_shader, const char *f)
{
    glslp_t *p = glslp_parse(f);

    if (p) {
        char path[512];
        char file[1024];
        int  failed = 0;
        strcpy(path, f);
        char *filename = path_get_filename(path);

        struct glsl_shader *gshader = &glsl->shaders[num_shader];

        strcpy(gshader->name, p->name);
        *filename = 0;

        gshader->num_lut_textures = p->num_textures;

        for (int i = 0; i < p->num_textures; ++i) {
            struct texture *texture = &p->textures[i];

            snprintf(file, sizeof(file) - 1, "%s%s", path, texture->path);

            struct shader_lut_texture *tex = &gshader->lut_textures[i];
            strcpy(tex->name, texture->name);

            ogl3_log("Load texture %s...\n", file);

            if (!load_texture(file, &tex->texture)) {
                // QMessageBox::critical(main_window, tr("GLSL Error"), tr("Could not load texture: %s").arg(file));
                main_window->showMessage(MBX_ERROR | MBX_FATAL, tr("GLSL Error"), tr("Could not load texture: %1").arg(file), false);
                ogl3_log("Could not load texture %s!\n", file);
                failed = 1;
                break;
            }

            if (!strcmp(texture->wrap_mode, "repeat"))
                tex->texture.wrap_mode = GL_REPEAT;
            else if (!strcmp(texture->wrap_mode, "mirrored_repeat"))
                tex->texture.wrap_mode = GL_MIRRORED_REPEAT;
            else if (!strcmp(texture->wrap_mode, "clamp_to_edge"))
                tex->texture.wrap_mode = GL_CLAMP_TO_EDGE;
            else
                tex->texture.wrap_mode = GL_CLAMP_TO_BORDER;

            tex->texture.mipmap = texture->mipmap;

            tex->texture.min_filter = tex->texture.mag_filter = texture->linear ? GL_LINEAR : GL_NEAREST;
            if (tex->texture.mipmap)
                tex->texture.min_filter = texture->linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;

            create_texture(&tex->texture);
            free(tex->texture.data);
            tex->texture.data = 0;
        }

        if (!failed) {
            gshader->input_filter_linear = p->input_filter_linear;

            gshader->num_parameters = p->num_parameters;
            for (int j = 0; j < gshader->num_parameters; ++j)
                memcpy(&gshader->parameters[j], &p->parameters[j], sizeof(struct shader_parameter));

            gshader->num_passes = p->num_shaders;

            for (int i = 0; i < p->num_shaders; ++i) {
                struct shader      *shader = &p->shaders[i];
                struct shader_pass *pass   = &gshader->passes[i];

                strcpy(pass->alias, shader->alias);
                if (!strlen(pass->alias))
                    snprintf(pass->alias, sizeof(pass->alias) - 1, "Pass %u", (i + 1));

                ogl3_log("Creating pass %u (%s)\n", (i + 1), pass->alias);
                ogl3_log("Loading shader %s...\n", shader->shader_fn);
                if (!shader->shader_program) {
                    main_window->showMessage(MBX_ERROR | MBX_FATAL, tr("GLSL Error"), tr("Could not load shader: %1").arg(shader->shader_fn), false);
                    // wx_simple_messagebox("GLSL Error", "Could not load shader: %s", shader->shader_fn);
                    ogl3_log("Could not load shader %s\n", shader->shader_fn);
                    failed = 1;
                    break;
                } else
                    ogl3_log("Shader %s loaded\n", shader->shader_fn);
                failed = !compile_shader(GL_VERTEX_SHADER, "#define VERTEX\n#define PARAMETER_UNIFORM\n",
                                         shader->shader_program, &pass->program.vertex_shader)
                    || !compile_shader(GL_FRAGMENT_SHADER, "#define FRAGMENT\n#define PARAMETER_UNIFORM\n",
                                       shader->shader_program, &pass->program.fragment_shader);
                if (failed)
                    break;

                if (!create_program(&pass->program)) {
                    failed = 1;
                    break;
                }
                pass->frame_count_mod  = shader->frame_count_mod;
                pass->fbo.mipmap_input = shader->mipmap_input;

                glw.glGenVertexArrays(1, (GLuint *) &pass->vertex_array);
                find_uniforms(gshader, i);
                setup_scale(shader, pass);
                if (i == p->num_shaders - 1) /* last pass may or may not be an fbo depending on scale */
                {
                    if (num_shader == glsl->num_shaders - 1) {
                        pass->fbo.id = -1;

                        for (uint8_t j = 0; j < 2; ++j) {
                            if (pass->scale.mode[j] != SCALE_SOURCE || pass->scale.value[j] != 1) {
                                setup_fbo(shader, &pass->fbo);
                                break;
                            }
                        }
                    } else {
                        /* check if next shaders' first pass wants the input mipmapped (will this ever
                         * happen?) */
                        pass->fbo.texture.mipmap = glsl->shaders[num_shader + 1].num_passes > 0 && glsl->shaders[num_shader + 1].passes[0].fbo.mipmap_input;
                        /* check if next shader wants the output of this pass to be filtered */
                        if (glsl->shaders[num_shader + 1].num_passes > 0 && glsl->shaders[num_shader + 1].input_filter_linear >= 0)
                            shader->filter_linear = glsl->shaders[num_shader + 1].input_filter_linear;
                        setup_fbo(shader, &pass->fbo);
                    }
                } else {
                    /* check if next pass wants the input mipmapped, if so we need to generate mipmaps of this
                     * pass */
                    pass->fbo.texture.mipmap = (i + 1) < p->num_shaders && p->shaders[i + 1].mipmap_input;
                    setup_fbo(shader, &pass->fbo);
                }
                if (pass->fbo.srgb)
                    glsl->srgb = 1;
                pass->active = 1;
            }
            if (!failed) {
                if (gshader->has_prev) {
                    struct shader scene_shader_conf;
                    memset(&scene_shader_conf, 0, sizeof(struct shader));
                    for (int i = 0; i < MAX_PREV; ++i) {
                        setup_fbo(&scene_shader_conf, &gshader->prev[i].fbo);
                    }
                }
            }
        }

        glslp_free(p);

        return glsl;
    }
    return 0;
}

glsl_t *
OpenGLRenderer::load_shaders(int num, char shaders[MAX_USER_SHADERS][512])
{
    glsl_t *glsl;

    glsl = (glsl_t *) malloc(sizeof(glsl_t));
    memset(glsl, 0, sizeof(glsl_t));

    glsl->num_shaders = num;
    int failed        = 0;
    for (int i = num - 1; i >= 0; --i) {
        const char *f = shaders[i];
        if (f && strlen(f)) {
            if (!load_glslp(glsl, i, f)) {
                failed = 1;
                break;
            }
        }
    }
    if (failed) {
        delete_glsl(glsl);
        memset(glsl, 0, sizeof(glsl_t));
    }
    return glsl;
}

void
OpenGLRenderer::read_shader_config()
{
    char s[512];
    for (int i = 0; i < active_shader->num_shaders; ++i) {
        struct glsl_shader *shader = &active_shader->shaders[i];
        char               *name   = shader->name;
        snprintf(s, sizeof(s) - 1, "GL3 Shaders - %s", name);
        //                shader->shader_refresh_rate = config_get_float(CFG_MACHINE, s, "shader_refresh_rate", -1);
        for (int j = 0; j < shader->num_parameters; ++j) {
            struct shader_parameter *param = &shader->parameters[j];
            param->value                   = config_get_double(s, param->id, param->default_value);
        }
    }
}

OpenGLRenderer::OpenGLRenderer(QWidget *parent)
    : QWindow((QWindow*)nullptr)
    , renderTimer(new QTimer(this))
{
    connect(renderTimer, &QTimer::timeout, this, [this]() { this->render(); });
    imagebufs[0] = std::unique_ptr<uint8_t>(new uint8_t[2048 * 2048 * 4]);
    imagebufs[1] = std::unique_ptr<uint8_t>(new uint8_t[2048 * 2048 * 4]);

    buf_usage = std::vector<std::atomic_flag>(2);
    buf_usage[0].clear();
    buf_usage[1].clear();

    QSurfaceFormat format;

    setSurfaceType(QWindow::OpenGLSurface);

#ifdef Q_OS_MACOS
    format.setVersion(4, 1);
#else
    format.setVersion(3, 2);
#endif
    format.setProfile(QSurfaceFormat::OpenGLContextProfile::CoreProfile);
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setSwapInterval(video_vsync ? 1 : 0);
    format.setAlphaBufferSize(0);

    setFormat(format);

    parentWidget = parent;

    source.setRect(0, 0, 100, 100);
    isInitialized = false;
    isFinalized   = false;
    context       = nullptr;
}

OpenGLRenderer::~OpenGLRenderer() { finalize(); }

void
OpenGLRenderer::initialize()
{
    try {
        context = new QOpenGLContext(this);

        context->setFormat(format());

        if (!context->create())
            throw opengl_init_error(tr("Couldn't create OpenGL context."));

        if (!context->makeCurrent(this))
            throw opengl_init_error(tr("Couldn't switch to OpenGL context."));

        auto version = context->format().version();

        if (version.first < 3)
            throw opengl_init_error(tr("OpenGL version 3.0 or greater is required. Current version is %1.%2").arg(version.first).arg(version.second));

        glw.initializeOpenGLFunctions();

        glw.glClearColor(0, 0, 0, 1);

        glw.glClear(GL_COLOR_BUFFER_BIT);

        ogl3_log("OpenGL information: [%s] %s (%s)\n", glw.glGetString(GL_VENDOR), glw.glGetString(GL_RENDERER), glw.glGetString(GL_VERSION));
        gl_version[0] = gl_version[1] = -1;
        glw.glGetIntegerv(GL_MAJOR_VERSION, &gl_version[0]);
        glw.glGetIntegerv(GL_MINOR_VERSION, &gl_version[1]);
        if (gl_version[0] < 3) {
            throw opengl_init_error(tr("OpenGL version 3.0 or greater is required. Current version is %1.%2").arg(gl_version[0]).arg(gl_version[1]));
        }
        ogl3_log("Using OpenGL %s\n", glw.glGetString(GL_VERSION));
        ogl3_log("Using Shading Language %s\n", glw.glGetString(GL_SHADING_LANGUAGE_VERSION));

        glslVersion = reinterpret_cast<const char *>(glw.glGetString(GL_SHADING_LANGUAGE_VERSION));
        glslVersion.truncate(4);
        glslVersion.remove('.');
        glslVersion.prepend("#version ");
        if (QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES)
            glslVersion.append(" es");
        else if (context->format().profile() == QSurfaceFormat::CoreProfile)
            glslVersion.append(" core");

        glw.glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
        ogl3_log("Max texture size: %dx%d\n", max_texture_size, max_texture_size);

        glw.glEnable(GL_TEXTURE_2D);

        // renderTimer->start(75);
        if (video_framerate != -1) {
            renderTimer->start(ceilf(1000.f / (float) video_framerate));
        }

        scene_texture.data            = NULL;
        scene_texture.width           = 2048;
        scene_texture.height          = 2048;
        scene_texture.internal_format = GL_RGBA8;
        scene_texture.format          = GL_RGBA;
        scene_texture.type            = GL_UNSIGNED_INT_8_8_8_8_REV;
        scene_texture.wrap_mode       = GL_CLAMP_TO_BORDER;
        scene_texture.min_filter = scene_texture.mag_filter = video_filter_method ? GL_LINEAR : GL_NEAREST;
        scene_texture.mipmap          = 0;

        create_texture(&scene_texture);

        /* load shader */
        //        const char* shaders[1];
        //        shaders[0] = gl3_shader_file;
        //
        //        active_shader = load_shaders(1, shaders);

        //        const char* shaders[3];
        //        shaders[0] = "/home/phantasy/git/glsl-shaders/ntsc/ntsc-320px.glslp";
        //        shaders[1] = "/home/phantasy/git/glsl-shaders/motionblur/motionblur-simple.glslp";
        //        shaders[2] = "/home/phantasy/git/glsl-shaders/crt/crt-lottes-multipass.glslp";
        //
        //        active_shader = load_shaders(3, shaders);
        int num_shaders = 0;
        for (int i = 0; i < MAX_USER_SHADERS; ++i) {
            if (strlen(gl3_shader_file[i]))
                ++num_shaders;
            else
                break;
        }
        active_shader = load_shaders(num_shaders, gl3_shader_file);

        create_scene_shader();

        /* read config */
        read_shader_config();

        /* buffers */

        GLfloat vertex[] = { -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f };

        GLfloat inv_vertex[] = { -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, -1.0f };

        GLfloat tex_coords[] = { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f };

        GLfloat colors[] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

        /* set the scene shader buffers */
        {
            glw.glBindVertexArray(active_shader->scene.vertex_array);

            struct shader_vbo *vbo = &active_shader->scene.vbo;

            glw.glGenBuffers(1, (GLuint *) &vbo->vertex_coord);
            glw.glBindBuffer(GL_ARRAY_BUFFER, vbo->vertex_coord);
            glw.glBufferData(GL_ARRAY_BUFFER, sizeof(inv_vertex), inv_vertex, GL_STATIC_DRAW);
            glw.glVertexAttribPointer(active_shader->scene.uniforms.vertex_coord, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat),
                                      (GLvoid *) 0);

            glw.glGenBuffers(1, (GLuint *) &vbo->tex_coord);
            glw.glBindBuffer(GL_ARRAY_BUFFER, vbo->tex_coord);
            glw.glBufferData(GL_ARRAY_BUFFER, sizeof(tex_coords), tex_coords, GL_DYNAMIC_DRAW);
            glw.glVertexAttribPointer(active_shader->scene.uniforms.tex_coord, 2, GL_FLOAT, GL_TRUE, 2 * sizeof(GLfloat),
                                      (GLvoid *) 0);
        }

        /* set buffers for all passes */
        for (int j = 0; j < active_shader->num_shaders; ++j) {
            struct glsl_shader *shader = &active_shader->shaders[j];
            for (int i = 0; i < shader->num_passes; ++i) {
                struct shader_uniforms *u = &shader->passes[i].uniforms;

                glw.glBindVertexArray(shader->passes[i].vertex_array);

                struct shader_vbo *vbo = &shader->passes[i].vbo;

                glw.glGenBuffers(1, (GLuint *) &vbo->vertex_coord);
                glw.glBindBuffer(GL_ARRAY_BUFFER, vbo->vertex_coord);
                glw.glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex, GL_STATIC_DRAW);

                glw.glVertexAttribPointer(u->vertex_coord, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (GLvoid *) 0);

                glw.glGenBuffers(1, (GLuint *) &vbo->tex_coord);
                glw.glBindBuffer(GL_ARRAY_BUFFER, vbo->tex_coord);
                glw.glBufferData(GL_ARRAY_BUFFER, sizeof(tex_coords), tex_coords, GL_DYNAMIC_DRAW);
                glw.glVertexAttribPointer(u->tex_coord, 2, GL_FLOAT, GL_TRUE, 2 * sizeof(GLfloat), (GLvoid *) 0);

                if (u->color) {
                    glw.glGenBuffers(1, (GLuint *) &vbo->color);
                    glw.glBindBuffer(GL_ARRAY_BUFFER, vbo->color);
                    glw.glBufferData(GL_ARRAY_BUFFER, sizeof(colors), colors, GL_STATIC_DRAW);
                    glw.glVertexAttribPointer(u->color, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (GLvoid *) 0);
                }
            }
        }

        for (int i = 0; i < active_shader->num_shaders; ++i) {
            struct glsl_shader *shader = &active_shader->shaders[i];
            if (shader->has_prev) {
                struct shader_pass *prev_pass = &shader->prev_scene;
                create_default_shader_tex(prev_pass);

                struct shader_vbo *vbo = &prev_pass->vbo;

                glw.glBindVertexArray(prev_pass->vertex_array);

                glw.glGenBuffers(1, (GLuint *) &vbo->vertex_coord);
                glw.glBindBuffer(GL_ARRAY_BUFFER, vbo->vertex_coord);
                glw.glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex, GL_STATIC_DRAW);
                glw.glVertexAttribPointer(prev_pass->uniforms.vertex_coord, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat),
                                          (GLvoid *) 0);

                glw.glGenBuffers(1, (GLuint *) &vbo->tex_coord);
                glw.glBindBuffer(GL_ARRAY_BUFFER, vbo->tex_coord);
                glw.glBufferData(GL_ARRAY_BUFFER, sizeof(tex_coords), tex_coords, GL_DYNAMIC_DRAW);
                glw.glVertexAttribPointer(prev_pass->uniforms.tex_coord, 2, GL_FLOAT, GL_TRUE, 2 * sizeof(GLfloat),
                                          (GLvoid *) 0);

                for (int j = 0; j < MAX_PREV; ++j) {
                    struct shader_prev *prev     = &shader->prev[j];
                    struct shader_vbo  *prev_vbo = &prev->vbo;

                    glw.glGenBuffers(1, (GLuint *) &prev_vbo->vertex_coord);
                    glw.glBindBuffer(GL_ARRAY_BUFFER, prev_vbo->vertex_coord);
                    glw.glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex, GL_STATIC_DRAW);

                    glw.glGenBuffers(1, (GLuint *) &prev_vbo->tex_coord);
                    glw.glBindBuffer(GL_ARRAY_BUFFER, prev_vbo->tex_coord);
                    glw.glBufferData(GL_ARRAY_BUFFER, sizeof(tex_coords), tex_coords, GL_DYNAMIC_DRAW);
                }
            }
        }

        /* create final pass */
        if (active_shader->num_shaders == 0 || active_shader->shaders[active_shader->num_shaders - 1].passes[active_shader->shaders[active_shader->num_shaders - 1].num_passes - 1].fbo.id >= 0) {
            struct shader_pass *final_pass = &active_shader->final_pass;
            create_default_shader_tex(final_pass);

            glw.glBindVertexArray(final_pass->vertex_array);

            struct shader_vbo *vbo = &final_pass->vbo;

            glw.glGenBuffers(1, (GLuint *) &vbo->vertex_coord);
            glw.glBindBuffer(GL_ARRAY_BUFFER, vbo->vertex_coord);
            glw.glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex, GL_STATIC_DRAW);
            glw.glVertexAttribPointer(final_pass->uniforms.vertex_coord, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat),
                                      (GLvoid *) 0);

            glw.glGenBuffers(1, (GLuint *) &vbo->tex_coord);
            glw.glBindBuffer(GL_ARRAY_BUFFER, vbo->tex_coord);
            glw.glBufferData(GL_ARRAY_BUFFER, sizeof(tex_coords), tex_coords, GL_DYNAMIC_DRAW);
            glw.glVertexAttribPointer(final_pass->uniforms.tex_coord, 2, GL_FLOAT, GL_TRUE, 2 * sizeof(GLfloat),
                                      (GLvoid *) 0);
        }

        {
            struct shader_pass *color_pass = &active_shader->fs_color;
            create_default_shader_color(color_pass);

            glw.glBindVertexArray(color_pass->vertex_array);

            struct shader_vbo *vbo = &color_pass->vbo;

            glw.glGenBuffers(1, (GLuint *) &vbo->vertex_coord);
            glw.glBindBuffer(GL_ARRAY_BUFFER, vbo->vertex_coord);
            glw.glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex, GL_STATIC_DRAW);
            glw.glVertexAttribPointer(color_pass->uniforms.vertex_coord, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat),
                                      (GLvoid *) 0);

            glw.glGenBuffers(1, (GLuint *) &vbo->color);
            glw.glBindBuffer(GL_ARRAY_BUFFER, vbo->color);
            glw.glBufferData(GL_ARRAY_BUFFER, sizeof(colors), colors, GL_DYNAMIC_DRAW);
            glw.glVertexAttribPointer(color_pass->uniforms.color, 4, GL_FLOAT, GL_TRUE, 4 * sizeof(GLfloat), (GLvoid *) 0);
        }
#ifdef SDL2_SHADER_DEBUG
        struct shader_pass *debug_pass = &active_shader->debug;
        create_default_shader(debug_pass);

        glw.glBindVertexArray(debug_pass->vertex_array);

        struct shader_vbo *vbo = &debug_pass->vbo;

        glw.glGenBuffers(1, &vbo->vertex_coord);
        glw.glBindBuffer(GL_ARRAY_BUFFER, vbo->vertex_coord);
        glw.glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex, GL_STATIC_DRAW);
        glw.glVertexAttribPointer(debug_pass->uniforms.vertex_coord, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (GLvoid *) 0);

        glw.glGenBuffers(1, &vbo->tex_coord);
        glw.glBindBuffer(GL_ARRAY_BUFFER, vbo->tex_coord);
        glw.glBufferData(GL_ARRAY_BUFFER, sizeof(tex_coords), tex_coords, GL_DYNAMIC_DRAW);
        glw.glVertexAttribPointer(debug_pass->uniforms.tex_coord, 2, GL_FLOAT, GL_TRUE, 2 * sizeof(GLfloat), (GLvoid *) 0);
#endif

        glw.glBindBuffer(GL_ARRAY_BUFFER, 0);
        glw.glBindVertexArray(0);

        isInitialized = true;
        isFinalized   = false;

        emit initialized();

        context->swapBuffers(this);
    } catch (const opengl_init_error &e) {
        /* Mark all buffers as in use */
        for (auto &flag : buf_usage)
            flag.test_and_set();

        main_window->showMessage(MBX_ERROR | MBX_FATAL, tr("Error initializing OpenGL"), e.what() + tr("\nFalling back to software rendering."), false);

        context->doneCurrent();
        isFinalized   = true;
        isInitialized = true;

        emit errorInitializing();
    }
}

void
OpenGLRenderer::finalize()
{
    if (isFinalized || !context)
        return;

    context->makeCurrent(this);

    delete_texture(&scene_texture);

    if (active_shader) {
        delete_glsl(active_shader);
        free(active_shader);
    }
    active_shader = NULL;

    context->doneCurrent();

    context = nullptr;

    isFinalized = true;
}

extern void take_screenshot_clipboard_monitor(int sx, int sy, int sw, int sh, int i);

void
OpenGLRenderer::onBlit(int buf_idx, int x, int y, int w, int h)
{
    if (notReady())
        return;

    context->makeCurrent(this);

    if (source.width() != w || source.height() != h) {
        glw.glBindTexture(GL_TEXTURE_2D, scene_texture.id);
        glw.glTexImage2D(GL_TEXTURE_2D, 0, (GLenum) QOpenGLTexture::RGB8_UNorm, w, h, 0, (GLenum) QOpenGLTexture::BGRA, (GLenum) QOpenGLTexture::UInt32_RGBA8_Rev, NULL);
        glw.glBindTexture(GL_TEXTURE_2D, 0);
    }

    source.setRect(x, y, w, h);

    glw.glBindTexture(GL_TEXTURE_2D, scene_texture.id);
    glw.glPixelStorei(GL_UNPACK_ROW_LENGTH, 2048);
    glw.glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, (GLenum) QOpenGLTexture::BGRA, (GLenum) QOpenGLTexture::UInt32_RGBA8_Rev, (const void *) ((uintptr_t) imagebufs[buf_idx].get() + (uintptr_t) (2048 * 4 * y + x * 4)));
    glw.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glw.glBindTexture(GL_TEXTURE_2D, 0);

    buf_usage[buf_idx].clear();
    source.setRect(x, y, w, h);
    this->pixelRatio = devicePixelRatio();
    onResize(this->width(), this->height());

#ifdef Q_OS_MACOS
    glw.glViewport(
        destination.x(),
        destination.y(),
        destination.width(),
        destination.height());
#endif

    if (video_framerate == -1)
        render();

    if (monitors[r_monitor_index].mon_screenshots_raw_clipboard) {
        take_screenshot_clipboard_monitor(x, y, w, h, r_monitor_index);
    }
}

std::vector<std::tuple<uint8_t *, std::atomic_flag *>>
OpenGLRenderer::getBuffers()
{
    std::vector<std::tuple<uint8_t *, std::atomic_flag *>> buffers;

    buffers.push_back(std::make_tuple(imagebufs[0].get(), &buf_usage[0]));
    buffers.push_back(std::make_tuple(imagebufs[1].get(), &buf_usage[1]));

    return buffers;
}

void
OpenGLRenderer::exposeEvent(QExposeEvent *event)
{
    Q_UNUSED(event);

    if (!isInitialized)
        initialize();

    this->pixelRatio = devicePixelRatio();
    onResize(size().width(), size().height());
}

void
OpenGLRenderer::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);

    this->pixelRatio = devicePixelRatio();
    onResize(event->size().width(), event->size().height());

    if (notReady())
        return;

    context->makeCurrent(this);

    glw.glViewport(
        destination.x(),
        destination.y(),
        destination.width(),
        destination.height());

    if (video_framerate == -1)
        render();
}

void
OpenGLRenderer::render_pass(struct render_data *data)
{
    GLuint texture_unit = 0;

    //        ogl3_log("pass %d: %gx%g, %gx%g -> %gx%g, %gx%g, %gx%g\n", num_pass, pass->state.input_size[0],
    //        pass->state.input_size[1], pass->state.input_texture_size[0], pass->state.input_texture_size[1],
    //        pass->state.output_size[0], pass->state.output_size[1], pass->state.output_texture_size[0],
    //        pass->state.output_texture_size[1], output_size[0], output_size[1]);

    glw.glBindVertexArray(data->shader_pass->vertex_array);

    GLint                   p = data->shader_pass->program.id;
    struct shader_uniforms *u = &data->shader_pass->uniforms;

    glw.glUseProgram(p);

    if (data->texture) {
        glw.glActiveTexture(GL_TEXTURE0 + texture_unit);
        glw.glBindTexture(GL_TEXTURE_2D, data->texture);
        glw.glUniform1i(u->texture, texture_unit);
        texture_unit++;
    }

    if (u->color >= 0)
        glw.glEnableVertexAttribArray(u->color);

    if (u->mvp_matrix >= 0)
        glw.glUniformMatrix4fv(u->mvp_matrix, 1, 0, matrix);
    if (u->frame_direction >= 0)
        glw.glUniform1i(u->frame_direction, 1);

    int framecnt = data->frame_count;
    if (data->shader_pass->frame_count_mod > 0)
        framecnt = framecnt % data->shader_pass->frame_count_mod;
    if (u->frame_count >= 0)
        glw.glUniform1i(u->frame_count, framecnt);

    if (u->input_size >= 0)
        glw.glUniform2fv(u->input_size, 1, data->shader_pass->state.input_size);
    if (u->texture_size >= 0)
        glw.glUniform2fv(u->texture_size, 1, data->shader_pass->state.input_texture_size);
    if (u->output_size >= 0)
        glw.glUniform2fv(u->output_size, 1, data->output_size);

    if (data->shader) {
        /* parameters */
        for (int i = 0; i < data->shader->num_parameters; ++i)
            if (u->parameters[i] >= 0)
                glw.glUniform1f(u->parameters[i], data->shader->parameters[i].value);

        if (data->pass > 0) {
            struct shader_pass *passes = data->shader->passes;
            struct shader_pass *orig   = data->orig_pass;
            if (u->orig.texture >= 0) {
                glw.glActiveTexture(GL_TEXTURE0 + texture_unit);
                glw.glBindTexture(GL_TEXTURE_2D, orig->fbo.texture.id);
                glw.glUniform1i(u->orig.texture, texture_unit);
                texture_unit++;
            }
            if (u->orig.input_size >= 0)
                glw.glUniform2fv(u->orig.input_size, 1, orig->state.input_size);
            if (u->orig.texture_size >= 0)
                glw.glUniform2fv(u->orig.texture_size, 1, orig->state.input_texture_size);

            for (int i = 0; i < data->pass; ++i) {
                if (u->pass[i].texture >= 0) {
                    glw.glActiveTexture(GL_TEXTURE0 + texture_unit);
                    glw.glBindTexture(GL_TEXTURE_2D, passes[i].fbo.texture.id);
                    glw.glUniform1i(u->pass[i].texture, texture_unit);
                    texture_unit++;
                }
                if (u->pass[i].texture_size >= 0)
                    glw.glUniform2fv(u->pass[i].texture_size, 1, passes[i].state.input_texture_size);
                if (u->pass[i].input_size >= 0)
                    glw.glUniform2fv(u->pass[i].input_size, 1, passes[i].state.input_size);

                if (u->prev_pass[i].texture >= 0) {
                    glw.glActiveTexture(GL_TEXTURE0 + texture_unit);
                    glw.glBindTexture(GL_TEXTURE_2D, passes[i].fbo.texture.id);
                    glw.glUniform1i(u->prev_pass[i].texture, texture_unit);
                    texture_unit++;
                }
                if (u->prev_pass[i].texture_size >= 0)
                    glw.glUniform2fv(u->prev_pass[i].texture_size, 1, passes[i].state.input_texture_size);
                if (u->prev_pass[i].input_size >= 0)
                    glw.glUniform2fv(u->prev_pass[i].input_size, 1, passes[i].state.input_size);
            }
        }

        if (data->shader->has_prev) {
            /* loop through each previous frame */
            for (int i = 0; i < MAX_PREV; ++i) {
                if (u->prev[i].texture >= 0) {
                    glw.glActiveTexture(GL_TEXTURE0 + texture_unit);
                    glw.glBindTexture(GL_TEXTURE_2D, data->shader->prev[i].fbo.texture.id);
                    glw.glUniform1i(u->prev[i].texture, texture_unit);
                    texture_unit++;
                }
                if (u->prev[i].tex_coord >= 0) {
                    glw.glBindBuffer(GL_ARRAY_BUFFER, data->shader->prev[i].vbo.tex_coord);
                    glw.glVertexAttribPointer(u->prev[i].tex_coord, 2, GL_FLOAT, GL_TRUE,
                                              2 * sizeof(GLfloat), (GLvoid *) 0);
                    glw.glEnableVertexAttribArray(u->prev[i].tex_coord);
                    glw.glBindBuffer(GL_ARRAY_BUFFER, 0);
                }
            }
        }

        for (int i = 0; i < data->shader->num_lut_textures; ++i) {
            if (u->lut_textures[i] >= 0) {
                glw.glActiveTexture(GL_TEXTURE0 + texture_unit);
                glw.glBindTexture(GL_TEXTURE_2D, data->shader->lut_textures[i].texture.id);
                glw.glUniform1i(u->lut_textures[i], texture_unit);
                texture_unit++;
            }
        }
    }

    glw.glEnableVertexAttribArray(u->vertex_coord);
    glw.glEnableVertexAttribArray(u->tex_coord);

    glw.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glw.glActiveTexture(GL_TEXTURE0);
    glw.glBindTexture(GL_TEXTURE_2D, 0);

    glw.glDisableVertexAttribArray(data->shader_pass->uniforms.vertex_coord);
    glw.glDisableVertexAttribArray(data->shader_pass->uniforms.tex_coord);
    if (data->shader_pass->uniforms.color >= 0)
        glw.glDisableVertexAttribArray(data->shader_pass->uniforms.color);

    if (data->shader && data->shader->has_prev) {
        for (int i = 0; i < MAX_PREV; ++i) {
            if (u->prev[i].tex_coord >= 0)
                glw.glDisableVertexAttribArray(u->prev[i].tex_coord);
        }
    }

    glw.glBindVertexArray(0);

    glw.glUseProgram(0);
}

bool
OpenGLRenderer::event(QEvent *event)
{
    Q_UNUSED(event);

    bool res = false;
    if (!eventDelegate(event, res))
        return QWindow::event(event);
    return res;
}

QDialog *
OpenGLRenderer::getOptions(QWidget *parent)
{
    return new OpenGLShaderManagerDialog(parent);
}

extern void standalone_scale(QRect &destination, int width, int height, QRect source, int scalemode);

void
OpenGLRenderer::render()
{
    if (!context)
        return;

    if (notReady())
        return;

    struct {
        uint32_t x;
        uint32_t y;
        uint32_t w;
        uint32_t h;
    } window_rect;

    window_rect.x = destination.x();
    window_rect.y = destination.y();
    window_rect.w = destination.width();
    window_rect.h = destination.height();

    glw.glBindTexture(GL_TEXTURE_2D, scene_texture.id);
    scene_texture.min_filter = scene_texture.mag_filter = video_filter_method ? GL_LINEAR : GL_NEAREST;
    active_shader->scene.fbo.texture.min_filter = active_shader->scene.fbo.texture.mag_filter = video_filter_method ? GL_LINEAR : GL_NEAREST;
    glw.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, video_filter_method ? GL_LINEAR : GL_NEAREST);
    glw.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, video_filter_method ? GL_LINEAR : GL_NEAREST);
    glw.glBindTexture(GL_TEXTURE_2D, active_shader->scene.fbo.texture.id);
    glw.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, video_filter_method ? GL_LINEAR : GL_NEAREST);
    glw.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, video_filter_method ? GL_LINEAR : GL_NEAREST);
    glw.glBindTexture(GL_TEXTURE_2D, 0);

    GLfloat orig_output_size[] = { (GLfloat) window_rect.w, (GLfloat) window_rect.h };

    if (active_shader->srgb)
        glw.glEnable(GL_FRAMEBUFFER_SRGB);

    struct render_data data;

    /* render scene to texture */
    {
        struct shader_pass *pass = &active_shader->scene;

        QRect rect;
        rect.setX(0);
        rect.setY(0);
        rect.setWidth(source.width() * video_gl_input_scale);
        rect.setHeight(source.height() * video_gl_input_scale);

        standalone_scale(rect, source.width(), source.height(), rect, video_gl_input_scale_mode);

        pass->state.input_size[0] = pass->state.output_size[0] = rect.width();
        pass->state.input_size[1] = pass->state.output_size[1] = rect.height();

        pass->state.input_texture_size[0] = pass->state.output_texture_size[0] = next_pow2(pass->state.output_size[0]);
        pass->state.input_texture_size[1] = pass->state.output_texture_size[1] = next_pow2(pass->state.output_size[1]);

        recreate_fbo(&active_shader->scene.fbo, pass->state.output_texture_size[0], pass->state.output_texture_size[1]);

        glw.glBindFramebuffer(GL_FRAMEBUFFER, active_shader->scene.fbo.id);
        glw.glClearColor(0, 0, 0, 1);
        glw.glClear(GL_COLOR_BUFFER_BIT);

        glw.glViewport(0, 0, pass->state.output_size[0], pass->state.output_size[1]);

        GLfloat minx = 0;
        GLfloat miny = 0;
        GLfloat maxx = pass->state.output_size[0] / (GLfloat) pass->state.output_texture_size[0];
        GLfloat maxy = pass->state.output_size[1] / (GLfloat) pass->state.output_texture_size[1];

        pass->state.tex_coords[0] = minx;
        pass->state.tex_coords[1] = miny;
        pass->state.tex_coords[2] = minx;
        pass->state.tex_coords[3] = maxy;
        pass->state.tex_coords[4] = maxx;
        pass->state.tex_coords[5] = miny;
        pass->state.tex_coords[6] = maxx;
        pass->state.tex_coords[7] = maxy;

        // create input tex coords
        minx = 0;
        miny = 0;
        maxx = 1;
        maxy = 1;

        GLfloat tex_coords[] = { minx, miny, minx, maxy, maxx, miny, maxx, maxy };

        glw.glBindVertexArray(pass->vertex_array);

        glw.glBindBuffer(GL_ARRAY_BUFFER, pass->vbo.tex_coord);
        glw.glBufferSubData(GL_ARRAY_BUFFER, 0, 8 * sizeof(GLfloat), tex_coords);
        glw.glBindBuffer(GL_ARRAY_BUFFER, 0);

        memset(&data, 0, sizeof(struct render_data));
        data.pass        = -1;
        data.shader_pass = &active_shader->scene;
        data.texture     = scene_texture.id;
        data.output_size = orig_output_size;
        render_pass(&data);

        glw.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    struct shader_pass *orig  = &active_shader->scene;
    struct shader_pass *input = &active_shader->scene;

    for (int s = 0; s < active_shader->num_shaders; ++s) {
        struct glsl_shader *shader = &active_shader->shaders[s];

        int frame_count = frameCounter;

        /* loop through each pass */
        for (int i = 0; i < shader->num_passes; ++i) {
            bool                resetFiltering = false;
            struct shader_pass *pass           = &shader->passes[i];

            memcpy(pass->state.input_size, input->state.output_size, 2 * sizeof(GLfloat));
            memcpy(pass->state.input_texture_size, input->state.output_texture_size, 2 * sizeof(GLfloat));

            for (uint8_t j = 0; j < 2; ++j) {
                if (pass->scale.mode[j] == SCALE_VIEWPORT)
                    pass->state.output_size[j] = orig_output_size[j] * pass->scale.value[j];
                else if (pass->scale.mode[j] == SCALE_ABSOLUTE)
                    pass->state.output_size[j] = pass->scale.value[j];
                else
                    pass->state.output_size[j] = pass->state.input_size[j] * pass->scale.value[j];

                pass->state.output_texture_size[j] = next_pow2(pass->state.output_size[j]);
            }

            if (pass->fbo.id >= 0) {
                recreate_fbo(&pass->fbo, pass->state.output_texture_size[0], pass->state.output_texture_size[1]);

                glw.glBindFramebuffer(GL_FRAMEBUFFER, pass->fbo.id);
                glw.glViewport(0, 0, pass->state.output_size[0], pass->state.output_size[1]);
            } else {
                resetFiltering = true;
                glw.glBindTexture(GL_TEXTURE_2D, input->fbo.texture.id);
                glw.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, video_filter_method ? GL_LINEAR : GL_NEAREST);
                glw.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, video_filter_method ? GL_LINEAR : GL_NEAREST);
                glw.glBindTexture(GL_TEXTURE_2D, 0);
                glw.glViewport(window_rect.x, window_rect.y, window_rect.w, window_rect.h);
            }

            glw.glClearColor(0, 0, 0, 1);
            glw.glClear(GL_COLOR_BUFFER_BIT);

            GLfloat minx = 0;
            GLfloat miny = 0;
            GLfloat maxx = pass->state.output_size[0] / (GLfloat) pass->state.output_texture_size[0];
            GLfloat maxy = pass->state.output_size[1] / (GLfloat) pass->state.output_texture_size[1];

            pass->state.tex_coords[0] = minx;
            pass->state.tex_coords[1] = miny;
            pass->state.tex_coords[2] = minx;
            pass->state.tex_coords[3] = maxy;
            pass->state.tex_coords[4] = maxx;
            pass->state.tex_coords[5] = miny;
            pass->state.tex_coords[6] = maxx;
            pass->state.tex_coords[7] = maxy;

            glw.glBindVertexArray(pass->vertex_array);

            glw.glBindBuffer(GL_ARRAY_BUFFER, pass->vbo.tex_coord);
            glw.glBufferSubData(GL_ARRAY_BUFFER, 0, 8 * sizeof(GLfloat), input->state.tex_coords);
            glw.glBindBuffer(GL_ARRAY_BUFFER, 0);

            memset(&data, 0, sizeof(struct render_data));
            data.shader      = shader;
            data.pass        = i;
            data.shader_pass = pass;
            data.texture     = input->fbo.texture.id;
            data.output_size = orig_output_size;
            data.orig_pass   = orig;
            data.frame_count = frame_count;

            render_pass(&data);

            glw.glBindFramebuffer(GL_FRAMEBUFFER, 0);

            if (pass->fbo.texture.mipmap) {
                glw.glActiveTexture(GL_TEXTURE0);
                glw.glBindTexture(GL_TEXTURE_2D, pass->fbo.texture.id);
                glw.glGenerateMipmap(GL_TEXTURE_2D);
                glw.glBindTexture(GL_TEXTURE_2D, 0);
            }

            if (resetFiltering) {
                glw.glBindTexture(GL_TEXTURE_2D, input->fbo.texture.id);
                glw.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, input->fbo.texture.min_filter);
                glw.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, input->fbo.texture.mag_filter);
                glw.glBindTexture(GL_TEXTURE_2D, 0);
            }

            input = pass;
        }

        if (shader->has_prev) {
            /* shift array */
            memmove(&shader->prev[1], &shader->prev[0], MAX_PREV * sizeof(struct shader_prev));
            memcpy(&shader->prev[0], &shader->prev[MAX_PREV], sizeof(struct shader_prev));

            struct shader_pass *pass      = orig;
            struct shader_pass *prev_pass = &shader->prev_scene;
            struct shader_prev *prev      = &shader->prev[0];

            memcpy(&prev_pass->state, &pass->state, sizeof(struct shader_state));

            recreate_fbo(&prev->fbo, prev_pass->state.output_texture_size[0],
                         prev_pass->state.output_texture_size[1]);

            memcpy(&prev_pass->fbo, &prev->fbo, sizeof(struct shader_fbo));

            glw.glBindFramebuffer(GL_FRAMEBUFFER, prev->fbo.id);
            glw.glClearColor(0, 0, 0, 1);
            glw.glClear(GL_COLOR_BUFFER_BIT);

            glw.glViewport(0, 0, pass->state.output_size[0], pass->state.output_size[1]);

            glw.glBindVertexArray(prev_pass->vertex_array);

            glw.glBindBuffer(GL_ARRAY_BUFFER, prev->vbo.tex_coord);
            glw.glBufferSubData(GL_ARRAY_BUFFER, 0, 8 * sizeof(GLfloat), pass->state.tex_coords);
            glw.glBindBuffer(GL_ARRAY_BUFFER, 0);

            glw.glBindBuffer(GL_ARRAY_BUFFER, prev_pass->vbo.tex_coord);
            glw.glBufferSubData(GL_ARRAY_BUFFER, 0, 8 * sizeof(GLfloat), pass->state.tex_coords);
            glw.glBindBuffer(GL_ARRAY_BUFFER, 0);

            memset(&data, 0, sizeof(struct render_data));
            data.shader      = shader;
            data.pass        = -10;
            data.shader_pass = prev_pass;
            data.texture     = pass->fbo.texture.id;
            data.output_size = orig_output_size;

            render_pass(&data);

            glw.glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        orig = input;
    }

    if (active_shader->final_pass.active) {
        struct shader_pass *pass = &active_shader->final_pass;

        memcpy(pass->state.input_size, input->state.output_size, 2 * sizeof(GLfloat));
        memcpy(pass->state.input_texture_size, input->state.output_texture_size, 2 * sizeof(GLfloat));

        for (uint8_t j = 0; j < 2; ++j) {
            if (pass->scale.mode[j] == SCALE_VIEWPORT)
                pass->state.output_size[j] = orig_output_size[j] * pass->scale.value[j];
            else if (pass->scale.mode[j] == SCALE_ABSOLUTE)
                pass->state.output_size[j] = pass->scale.value[j];
            else
                pass->state.output_size[j] = pass->state.input_size[j] * pass->scale.value[j];

            pass->state.output_texture_size[j] = next_pow2(pass->state.output_size[j]);
        }

        glw.glBindTexture(GL_TEXTURE_2D, input->fbo.texture.id);
        glw.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, video_filter_method ? GL_LINEAR : GL_NEAREST);
        glw.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, video_filter_method ? GL_LINEAR : GL_NEAREST);
        glw.glBindTexture(GL_TEXTURE_2D, 0);

        glw.glViewport(window_rect.x, window_rect.y, window_rect.w, window_rect.h);

        glw.glClearColor(0, 0, 0, 1);
        glw.glClear(GL_COLOR_BUFFER_BIT);

        GLfloat minx = 0;
        GLfloat miny = 0;
        GLfloat maxx = pass->state.output_size[0] / (GLfloat) pass->state.output_texture_size[0];
        GLfloat maxy = pass->state.output_size[1] / (GLfloat) pass->state.output_texture_size[1];

        pass->state.tex_coords[0] = minx;
        pass->state.tex_coords[1] = miny;
        pass->state.tex_coords[2] = minx;
        pass->state.tex_coords[3] = maxy;
        pass->state.tex_coords[4] = maxx;
        pass->state.tex_coords[5] = miny;
        pass->state.tex_coords[6] = maxx;
        pass->state.tex_coords[7] = maxy;

        glw.glBindVertexArray(pass->vertex_array);

        glw.glBindBuffer(GL_ARRAY_BUFFER, pass->vbo.tex_coord);
        glw.glBufferSubData(GL_ARRAY_BUFFER, 0, 8 * sizeof(GLfloat), input->state.tex_coords);
        glw.glBindBuffer(GL_ARRAY_BUFFER, 0);

        memset(&data, 0, sizeof(struct render_data));
        data.pass        = -2;
        data.shader_pass = pass;
        data.texture     = input->fbo.texture.id;
        data.output_size = orig_output_size;
        data.orig_pass   = orig;

        render_pass(&data);
    }

    if (monitors[r_monitor_index].mon_screenshots) {
        int  width = destination.width(), height = destination.height();
        char path[1024];
        char fn[256];

        memset(fn, 0, sizeof(fn));
        memset(path, 0, sizeof(path));

        path_append_filename(path, usr_path, SCREENSHOT_PATH);

        if (!plat_dir_check(path))
            plat_dir_create(path);

        path_slash(path);
        strcat(path, "Monitor_");
        snprintf(&path[strlen(path)], 42, "%d_", r_monitor_index + 1);

        plat_tempfile(fn, NULL, (char *) ".png");
        strcat(path, fn);

        unsigned char *rgb = (unsigned char *) calloc(1, (size_t) width * height * 4);

        glw.glFinish();
        glw.glReadPixels(window_rect.x, window_rect.y, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb);

        int pitch_adj = (4 - ((width * 3) & 3)) & 3;
        QImage image((uchar*)rgb, width, height, (width * 3) + pitch_adj, QImage::Format_RGB888);
        image.mirrored(false, true).save(path, "png");
        monitors[r_monitor_index].mon_screenshots--;
        free(rgb);
    }
    if (monitors[r_monitor_index].mon_screenshots_clipboard) {
        int  width = destination.width(), height = destination.height();

        unsigned char *rgb = (unsigned char *) calloc(1, (size_t) width * height * 4);

        glw.glFinish();
        glw.glReadPixels(window_rect.x, window_rect.y, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb);

        int pitch_adj = (4 - ((width * 3) & 3)) & 3;
        QImage image((uchar*)rgb, width, height, (width * 3) + pitch_adj, QImage::Format_RGB888);
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setImage(image.mirrored(false, true), QClipboard::Clipboard);
        monitors[r_monitor_index].mon_screenshots_clipboard--;
        free(rgb);
    }

    glw.glDisable(GL_FRAMEBUFFER_SRGB);

    frameCounter++;
    context->swapBuffers(this);
}

