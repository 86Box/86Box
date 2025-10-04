/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header file for OpenGL renderer
 *
 * Authors: Teemu Korhonen
 *          Cacodemon345
 *
 *          Copyright 2022 Teemu Korhonen
 *          Copyright 2025 Cacodemon345
 */
#ifndef QT_OpenGLRenderer_HPP
#define QT_OpenGLRenderer_HPP

#if defined Q_OS_MACOS || __arm__
#    define NO_BUFFER_STORAGE
#endif

#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QResizeEvent>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#if !defined NO_BUFFER_STORAGE && !(QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#    include <QtOpenGLExtensions/QOpenGLExtensions>
#endif

#include <array>
#include <atomic>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "qt_renderercommon.hpp"

extern "C" {
#include <86box/qt-glslp-parser.h>
}

struct render_data {
    int                 pass;
    struct glsl_shader *shader;
    struct shader_pass *shader_pass;
    GLfloat            *output_size;
    struct shader_pass *orig_pass;
    GLint               texture;
    int                 frame_count;
};

class OpenGLRenderer : public QWindow, public RendererCommon {
    Q_OBJECT

public:
    QOpenGLContext *context;

    OpenGLRenderer(QWidget *parent = nullptr);
    ~OpenGLRenderer();

    std::vector<std::tuple<uint8_t *, std::atomic_flag *>> getBuffers() override;

    void     finalize() override final;
    bool     hasOptions() const override { return true; }
    QDialog *getOptions(QWidget *parent) override;
    bool     reloadRendererOption() override { return true; }

signals:
    void initialized();
    void errorInitializing();

public slots:
    void onBlit(int buf_idx, int x, int y, int w, int h);

protected:
    void exposeEvent(QExposeEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;

private:
    std::array<std::unique_ptr<uint8_t>, 2> imagebufs;

    QTimer *renderTimer;

    QString glslVersion = "";

    bool isInitialized = false;
    bool isFinalized   = false;

    int max_texture_size = 65536;
    int frameCounter     = 0;

    QOpenGLExtraFunctions glw;
    struct shader_texture scene_texture;
    glsl_t               *active_shader;

    void *unpackBuffer = nullptr;

    int gl_version[2] = { 0, 0 };

    void initialize();
    void initializeExtensions();
    void initializeBuffers();
    void applyOptions();

    void create_scene_shader();
    void create_texture(struct shader_texture *tex);
    void create_fbo(struct shader_fbo *fbo);
    void recreate_fbo(struct shader_fbo *fbo, int width, int height);
    void setup_fbo(struct shader *shader, struct shader_fbo *fbo);

    bool    notReady() const { return !isInitialized || isFinalized; }
    glsl_t *load_glslp(glsl_t *glsl, int num_shader, const char *f);
    glsl_t *load_shaders(int num, char shaders[MAX_USER_SHADERS][512]);
    int     compile_shader(GLenum shader_type, const char *prepend, const char *program, int *dst);
    int     create_default_shader_tex(struct shader_pass *pass);
    int     create_default_shader_color(struct shader_pass *pass);
    int     create_program(struct shader_program *program);

    GLuint get_uniform(GLuint program, const char *name);
    GLuint get_attrib(GLuint program, const char *name);

    void find_uniforms(struct glsl_shader *glsl, int num_pass);
    void delete_texture(struct shader_texture *tex);
    void delete_fbo(struct shader_fbo *fbo);
    void delete_program(struct shader_program *program);
    void delete_vbo(struct shader_vbo *vbo);
    void delete_pass(struct shader_pass *pass);
    void delete_prev(struct shader_prev *prev);
    void delete_shader(struct glsl_shader *glsl);
    void delete_glsl(glsl_t *glsl);
    void read_shader_config();

    void render_pass(struct render_data *data);

private slots:
    void render();
};

class opengl_init_error : public std::runtime_error {
public:
    opengl_init_error(const QString &what)
        : std::runtime_error(what.toStdString())
    {
    }
};

#endif
