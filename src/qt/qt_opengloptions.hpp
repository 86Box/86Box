/*
 * 86Box A hypervisor and IBM PC system emulator that specializes in
 *      running old operating systems and software designed for IBM
 *      PC systems and compatibles from 1981 through fairly recent
 *      system designs based on the PCI bus.
 *
 *      This file is part of the 86Box distribution.
 *
 *      Header for OpenGL renderer options
 *
 * Authors:
 *      Teemu Korhonen
 *
 *      Copyright 2022 Teemu Korhonen
 */

#ifndef QT_OPENGLOPTIONS_HPP
#define QT_OPENGLOPTIONS_HPP

#include <QList>
#include <QObject>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>

class OpenGLShaderPass {
public:
    OpenGLShaderPass(QOpenGLShaderProgram *shader, const QString &path)
        : m_shader(shader)
        , m_path(path)
        , m_vertex_coord(shader->attributeLocation("VertexCoord"))
        , m_tex_coord(shader->attributeLocation("TexCoord"))
        , m_color(shader->attributeLocation("Color"))
        , m_mvp_matrix(shader->uniformLocation("MVPMatrix"))
        , m_input_size(shader->uniformLocation("InputSize"))
        , m_output_size(shader->uniformLocation("OutputSize"))
        , m_texture_size(shader->uniformLocation("TextureSize"))
        , m_frame_count(shader->uniformLocation("FrameCount"))
    {
    }

    bool           bind() const { return m_shader->bind(); }
    const QString &path() const { return m_path; }
    const GLint   &vertex_coord() const { return m_vertex_coord; }
    const GLint   &tex_coord() const { return m_tex_coord; }
    const GLint   &color() const { return m_color; }
    const GLint   &mvp_matrix() const { return m_mvp_matrix; }
    const GLint   &input_size() const { return m_input_size; }
    const GLint   &output_size() const { return m_output_size; }
    const GLint   &texture_size() const { return m_texture_size; }
    const GLint   &frame_count() const { return m_frame_count; }

private:
    QOpenGLShaderProgram *m_shader;
    QString               m_path;
    GLint                 m_vertex_coord;
    GLint                 m_tex_coord;
    GLint                 m_color;
    GLint                 m_mvp_matrix;
    GLint                 m_input_size;
    GLint                 m_output_size;
    GLint                 m_texture_size;
    GLint                 m_frame_count;
};

class OpenGLOptions : public QObject {
    Q_OBJECT

public:
    enum RenderBehaviorType { SyncWithVideo,
                              TargetFramerate };

    enum FilterType { Nearest,
                      Linear };

    OpenGLOptions(QObject *parent, bool loadConfig, const QString &glslVersion);

    RenderBehaviorType renderBehavior() const { return m_renderBehavior; }
    int                framerate() const { return m_framerate; }
    bool               vSync() const { return m_vsync; }
    FilterType         filter() const;

    const QList<OpenGLShaderPass> &shaders() const { return m_shaders; }

    void setRenderBehavior(RenderBehaviorType value);
    void setFrameRate(int value);
    void setVSync(bool value);
    void setFilter(FilterType value);
    void addShader(const QString &path);
    void addDefaultShader();
    void save() const;

private:
    RenderBehaviorType      m_renderBehavior = SyncWithVideo;
    int                     m_framerate      = -1;
    bool                    m_vsync          = false;
    FilterType              m_filter         = Nearest;
    QList<OpenGLShaderPass> m_shaders;
    QString                 m_glslVersion;
};

#endif
