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
#include <QOpenGLShaderProgram>

struct OpenGLShaderPass {
    OpenGLShaderPass(QOpenGLShaderProgram *shader, const QString &path)
        : shader(shader)
        , path(path)
        , vertex_coord(shader->attributeLocation("VertexCoord"))
        , tex_coord(shader->attributeLocation("TexCoord"))
        , color(shader->attributeLocation("Color"))
        , mvp_matrix(shader->uniformLocation("MVPMatrix"))
        , input_size(shader->uniformLocation("InputSize"))
        , output_size(shader->uniformLocation("OutputSize"))
        , texture_size(shader->uniformLocation("TextureSize"))
        , frame_count(shader->uniformLocation("FrameCount"))
    {
    }

    QOpenGLShaderProgram *shader;
    const QString         path;
    const GLint           vertex_coord;
    const GLint           tex_coord;
    const GLint           color;
    const GLint           mvp_matrix;
    const GLint           input_size;
    const GLint           output_size;
    const GLint           texture_size;
    const GLint           frame_count;
};

class OpenGLOptions : public QObject {
    Q_OBJECT

public:
    enum RenderBehaviorType { SyncWithVideo,
                              TargetFramerate };

    enum FilterType { Nearest,
                      Linear };

    OpenGLOptions(QObject *parent = nullptr, bool loadConfig = false);

    RenderBehaviorType renderBehavior() const { return m_renderBehavior; }
    int                framerate() const { return m_framerate; }
    bool               vSync() const { return m_vsync; }
    FilterType         filter() const;

    QList<OpenGLShaderPass> shaders() const { return m_shaders; };

    void setRenderBehavior(RenderBehaviorType value);
    void setFrameRate(int value);
    void setVSync(bool value);
    void setFilter(FilterType value);
    void addShader(const QString &path);
    void addDefaultShader();
    void save();

private:
    RenderBehaviorType      m_renderBehavior = SyncWithVideo;
    int                     m_framerate      = -1;
    bool                    m_vsync          = false;
    FilterType              m_filter         = Nearest;
    QList<OpenGLShaderPass> m_shaders;
};

#endif
