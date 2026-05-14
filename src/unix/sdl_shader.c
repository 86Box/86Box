/*
 * GLES2 shader renderer for 86Box SDL2 frontend.
 * Loads libretro-style .glslp/.glsl shaders and renders the emulator
 * framebuffer through them using OpenGL ES 2.0.
 */
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unix_sdl_shader.h"

static SDL_GLContext gl_ctx;
static GLuint        prog;
static GLuint        vbo;
static GLuint        fb_tex;
static int           is_active;
static int           tex_w, tex_h;
static int           frame_count;
static int           use_bgra;
static uint8_t      *packed_pixels;
static size_t        packed_pixels_size;

/* FBO for OSD compositing */
static GLuint osd_fbo;
static int    osd_fbo_inited;

/* Last blit viewport (for mouse coordinate mapping) */
static int last_dst_x, last_dst_y, last_dst_w, last_dst_h;

/* Uniform locations */
static GLint u_mvp, u_frame_dir, u_frame_cnt;
static GLint u_out_size, u_tex_size, u_in_size, u_sampler;

/* Attribute locations */
static GLint a_vtx, a_tc;

extern void osd_present(int fb_w, int fb_h);

static char *
read_text_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if ((long) fread(buf, 1, sz, f) != sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

/* Resolve shader0 path from a .glslp preset file.
   Returns a malloc'd absolute path, or NULL on failure. */
static char *
resolve_glslp(const char *glslp_path)
{
    char *content = read_text_file(glslp_path);
    if (!content)
        return NULL;

    char *p = strstr(content, "shader0");
    if (!p) { free(content); return NULL; }
    p = strchr(p, '=');
    if (!p) { free(content); return NULL; }
    p++;
    while (*p == ' ' || *p == '\t') p++;

    char *end = p;
    while (*end && *end != '\n' && *end != '\r') end++;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) end--;

    int name_len = (int) (end - p);

    /* Build absolute path relative to the .glslp directory. */
    size_t dir_len = 0;
    const char *slash = strrchr(glslp_path, '/');
    if (slash)
        dir_len = (size_t) (slash - glslp_path) + 1;

    char *result = malloc(dir_len + name_len + 1);
    if (!result) {
        free(content);
        return NULL;
    }
    if (dir_len)
        memcpy(result, glslp_path, dir_len);
    memcpy(result + dir_len, p, name_len);
    result[dir_len + name_len] = '\0';

    free(content);
    return result;
}

static GLuint
compile_shader(GLenum type, const char *source, const char *defines)
{
    GLuint sh = glCreateShader(type);
    const char *srcs[2] = {
        defines,
        source
    };
    glShaderSource(sh, 2, srcs, NULL);
    glCompileShader(sh);

    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "SDL Shader: compile error:\n%s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint
link_program(GLuint vs, GLuint fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);

    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "SDL Shader: link error:\n%s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static int
has_extension(const char *name)
{
    const char *exts = (const char *) glGetString(GL_EXTENSIONS);
    if (!exts)
        return 0;
    size_t len = strlen(name);
    const char *p = exts;
    while ((p = strstr(p, name)) != NULL) {
        if ((p == exts || p[-1] == ' ') &&
            (p[len] == '\0' || p[len] == ' '))
            return 1;
        p += len;
    }
    return 0;
}

/* Set each #pragma parameter uniform to its declared default value.
   Format: #pragma parameter NAME "Description" DEFAULT MIN MAX STEP */
static void
set_parameter_defaults(GLuint program, const char *source)
{
    const char *p = source;
    while ((p = strstr(p, "#pragma parameter")) != NULL) {
        p += 17; /* strlen("#pragma parameter") */
        while (*p == ' ' || *p == '\t') p++;

        /* Extract parameter name. */
        const char *ns = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        size_t nlen = (size_t) (p - ns);
        if (nlen == 0 || nlen >= 128)
            continue;
        char name[128];
        memcpy(name, ns, nlen);
        name[nlen] = '\0';

        /* Skip quoted description. */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') p++;
            if (*p) p++;
        }

        /* Parse default value. */
        while (*p == ' ' || *p == '\t') p++;
        char *end;
        float val = strtof(p, &end);
        if (end == p)
            continue;

        GLint loc = glGetUniformLocation(program, name);
        if (loc >= 0)
            glUniform1f(loc, val);
    }
}

/* Apply parameter overrides from a .glslp preset file.
   Scans for lines of the form:  NAME = VALUE  or  NAME = "VALUE"
   and sets matching uniforms.  Skips known .glslp meta-keys. */
static void
apply_glslp_overrides(GLuint program, const char *glslp_path)
{
    char *content = read_text_file(glslp_path);
    if (!content)
        return;

    static const char *skip_keys[] = {
        "shaders", "shader0", "filter_linear0", "scale_type0",
        "scale0", "wrap_mode0", "mipmap_input0", "parameters", NULL
    };

    const char *line = content;
    while (*line) {
        while (*line == ' ' || *line == '\t') line++;

        const char *eq = NULL;
        for (const char *c = line; *c && *c != '\n' && *c != '\r'; c++) {
            if (*c == '=') {
                eq = c;
                break;
            }
        }
        if (!eq) {
            while (*line && *line != '\n') line++;
            if (*line) line++;
            continue;
        }

        const char *ke = eq - 1;
        while (ke >= line && (*ke == ' ' || *ke == '\t')) ke--;
        size_t klen = (size_t) (ke - line) + 1;
        if (klen == 0 || klen >= 128) {
            while (*line && *line != '\n') line++;
            if (*line) line++;
            continue;
        }
        char key[128];
        memcpy(key, line, klen);
        key[klen] = '\0';

        int is_meta = 0;
        for (const char **sk = skip_keys; *sk; sk++)
            if (strcmp(key, *sk) == 0) {
                is_meta = 1;
                break;
            }
        if (is_meta) {
            while (*line && *line != '\n') line++;
            if (*line) line++;
            continue;
        }

        const char *vs = eq + 1;
        while (*vs == ' ' || *vs == '\t' || *vs == '"') vs++;
        char *vend;
        float val = strtof(vs, &vend);
        if (vend != vs) {
            GLint loc = glGetUniformLocation(program, key);
            if (loc >= 0)
                glUniform1f(loc, val);
        }

        while (*line && *line != '\n') line++;
        if (*line) line++;
    }
    free(content);
}

int
sdl_shader_init(SDL_Window *win, const char *shader_path)
{
    char *glsl_path = NULL;
    const char *ext = strrchr(shader_path, '.');
    if (ext && strcmp(ext, ".glslp") == 0)
        glsl_path = resolve_glslp(shader_path);
    else
        glsl_path = strdup(shader_path);

    if (!glsl_path) {
        fprintf(stderr, "SDL Shader: cannot resolve path from '%s'\n", shader_path);
        return 0;
    }

    char *source = read_text_file(glsl_path);
    if (!source) {
        fprintf(stderr, "SDL Shader: cannot load '%s'\n", glsl_path);
        free(glsl_path);
        return 0;
    }
    free(glsl_path);

    SDL_DisplayMode dm;
    int disp_idx = SDL_GetWindowDisplayIndex(win);
    if (disp_idx < 0)
        disp_idx = 0;

    if (SDL_GetDesktopDisplayMode(disp_idx, &dm) == 0 && dm.w > 0 && dm.h > 0) {
        SDL_DisplayMode cur_dm;
        Uint32 flags = SDL_GetWindowFlags(win);
        int is_fullscreen_desktop = ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP);
        int need_set_mode = 1;
        int need_set_fs = ((flags & SDL_WINDOW_FULLSCREEN) == 0) || is_fullscreen_desktop;

        if (SDL_GetWindowDisplayMode(win, &cur_dm) == 0 && cur_dm.w == dm.w && cur_dm.h == dm.h)
            need_set_mode = 0;

        if (need_set_mode)
            SDL_SetWindowDisplayMode(win, &dm);
        if (need_set_fs)
            SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN);

    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    gl_ctx = SDL_GL_CreateContext(win);
    if (!gl_ctx) {
        fprintf(stderr, "SDL Shader: GL context failed: %s\n", SDL_GetError());
        free(source);
        return 0;
    }
    SDL_GL_MakeCurrent(win, gl_ctx);
    SDL_GL_SetSwapInterval(0);

    use_bgra = has_extension("GL_EXT_texture_format_BGRA8888");

    GLuint vs = compile_shader(GL_VERTEX_SHADER, source,
                               "#define VERTEX\n#define PARAMETER_UNIFORM\n");
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, source,
                               "#define FRAGMENT\n#define PARAMETER_UNIFORM\n");

    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        free(source);
        SDL_GL_DeleteContext(gl_ctx);
        gl_ctx = NULL;
        return 0;
    }

    prog = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!prog) {
        free(source);
        SDL_GL_DeleteContext(gl_ctx);
        gl_ctx = NULL;
        return 0;
    }

    glUseProgram(prog);
    set_parameter_defaults(prog, source);
    free(source);

    if (ext && strcmp(ext, ".glslp") == 0)
        apply_glslp_overrides(prog, shader_path);

    a_vtx = glGetAttribLocation(prog, "VertexCoord");
    a_tc  = glGetAttribLocation(prog, "TexCoord");

    u_mvp       = glGetUniformLocation(prog, "MVPMatrix");
    u_frame_dir = glGetUniformLocation(prog, "FrameDirection");
    u_frame_cnt = glGetUniformLocation(prog, "FrameCount");
    u_out_size  = glGetUniformLocation(prog, "OutputSize");
    u_tex_size  = glGetUniformLocation(prog, "TextureSize");
    u_in_size   = glGetUniformLocation(prog, "InputSize");
    u_sampler   = glGetUniformLocation(prog, "Texture");

    static const float verts[] = {
        -1.0f, -1.0f, 0, 1, 0.0f, 1.0f, 0, 0,
         1.0f, -1.0f, 0, 1, 1.0f, 1.0f, 0, 0,
        -1.0f,  1.0f, 0, 1, 0.0f, 0.0f, 0, 0,
         1.0f,  1.0f, 0, 1, 1.0f, 0.0f, 0, 0,
    };
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glGenTextures(1, &fb_tex);
    glBindTexture(GL_TEXTURE_2D, fb_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    tex_w = tex_h = 0;
    frame_count = 0;
    is_active = 1;
    return 1;
}

void
sdl_shader_blit(SDL_Window *win, const void *pixels,
                int src_w, int src_h,
                int dst_x, int dst_y, int dst_w, int dst_h)
{
    if (!is_active)
        return;

    SDL_GL_MakeCurrent(win, gl_ctx);

    glBindTexture(GL_TEXTURE_2D, fb_tex);
    if (src_w != tex_w || src_h != tex_h) {
        GLenum ifmt = use_bgra ? GL_BGRA_EXT : GL_RGBA;
        GLenum fmt = ifmt;
        glTexImage2D(GL_TEXTURE_2D, 0, ifmt,
                     src_w, src_h, 0, fmt,
                     GL_UNSIGNED_BYTE, NULL);
        tex_w = src_w;
        tex_h = src_h;
    }

    GLenum fmt = use_bgra ? GL_BGRA_EXT : GL_RGBA;
    size_t needed = (size_t) src_w * (size_t) src_h * 4;
    if (packed_pixels_size < needed) {
        uint8_t *new_buf = realloc(packed_pixels, needed);
        if (!new_buf)
            return;
        packed_pixels = new_buf;
        packed_pixels_size = needed;
    }
    for (int y = 0; y < src_h; y++)
        memcpy(packed_pixels + (size_t) y * (size_t) src_w * 4,
               (const uint8_t *) pixels + (size_t) y * 2048 * 4,
               (size_t) src_w * 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, src_w, src_h,
                    fmt, GL_UNSIGNED_BYTE, packed_pixels);

    /* Composite OSD into fb_tex via FBO at emulator resolution
       so the shader (CRT etc.) applies to the OSD. */
    sdl_shader_begin_osd(src_w, src_h);
    osd_present(src_w, src_h);
    sdl_shader_end_osd();

    int win_w, win_h;
    SDL_GL_GetDrawableSize(win, &win_w, &win_h);

    /* Store blit viewport for OSD mouse coordinate mapping. */
    last_dst_x = dst_x;
    last_dst_y = dst_y;
    last_dst_w = dst_w;
    last_dst_h = dst_h;

    glViewport(0, 0, win_w, win_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    int gl_y = win_h - dst_y - dst_h;
    glViewport(dst_x, gl_y, dst_w, dst_h);

    glUseProgram(prog);

    static const float mvp[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    if (u_mvp >= 0) glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
    if (u_frame_dir >= 0) glUniform1i(u_frame_dir, 1);
    if (u_frame_cnt >= 0) glUniform1i(u_frame_cnt, frame_count);
    frame_count++;
    if (u_out_size >= 0) glUniform2f(u_out_size, (float) dst_w, (float) dst_h);
    if (u_tex_size >= 0) glUniform2f(u_tex_size, (float) src_w, (float) src_h);
    if (u_in_size >= 0) glUniform2f(u_in_size, (float) src_w, (float) src_h);
    if (u_sampler >= 0) glUniform1i(u_sampler, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fb_tex);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    if (a_vtx >= 0) {
        glEnableVertexAttribArray(a_vtx);
        glVertexAttribPointer(a_vtx, 4, GL_FLOAT, GL_FALSE,
                              8 * sizeof(float), (void *) 0);
    }
    if (a_tc >= 0) {
        glEnableVertexAttribArray(a_tc);
        glVertexAttribPointer(a_tc, 4, GL_FLOAT, GL_FALSE,
                              8 * sizeof(float), (void *) (4 * sizeof(float)));
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (a_vtx >= 0) glDisableVertexAttribArray(a_vtx);
    if (a_tc >= 0) glDisableVertexAttribArray(a_tc);

    SDL_GL_SwapWindow(win);
}

void
sdl_shader_close(void)
{
    if (!is_active)
        return;

    if (fb_tex) {
        glDeleteTextures(1, &fb_tex);
        fb_tex = 0;
    }
    if (osd_fbo) {
        glDeleteFramebuffers(1, &osd_fbo);
        osd_fbo = 0;
        osd_fbo_inited = 0;
    }
    if (vbo) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (prog) {
        glDeleteProgram(prog);
        prog = 0;
    }
    if (gl_ctx) {
        SDL_GL_DeleteContext(gl_ctx);
        gl_ctx = NULL;
    }
    if (packed_pixels) {
        free(packed_pixels);
        packed_pixels = NULL;
    }
    packed_pixels_size = 0;
    tex_w = tex_h = 0;
    is_active = 0;
}

int
sdl_shader_active(void)
{
    return is_active;
}

/* ------------------------------------------------------------------ */
/*  FBO for OSD compositing                                            */
/* ------------------------------------------------------------------ */
void
sdl_shader_begin_osd(int src_w, int src_h)
{
    if (!is_active || !fb_tex)
        return;

    if (!osd_fbo_inited) {
        glGenFramebuffers(1, &osd_fbo);
        osd_fbo_inited = 1;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, osd_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, fb_tex, 0);
    glViewport(0, 0, src_w, src_h);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void
sdl_shader_end_osd(void)
{
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* ------------------------------------------------------------------ */
/*  Software-rendered OSD overlay (used by old OSD, non-IMGUI)         */
/* ------------------------------------------------------------------ */
static GLuint ovl_prog;
static GLuint ovl_tex;
static GLuint ovl_vbo;
static int    ovl_a_vtx = -1;
static int    ovl_a_tc  = -1;
static int    ovl_u_sampler = -1;
static int    ovl_inited;

static int
ovl_init(void)
{
    if (ovl_inited)
        return 1;

    static const char *ovl_vs =
        "attribute vec4 VertexCoord;\n"
        "attribute vec4 TexCoord;\n"
        "varying vec2 v_tc;\n"
        "void main() {\n"
        "    gl_Position = VertexCoord;\n"
        "    v_tc = TexCoord.xy;\n"
        "}\n";
    static const char *ovl_fs =
        "precision mediump float;\n"
        "varying vec2 v_tc;\n"
        "uniform sampler2D Texture;\n"
        "void main() {\n"
        "    gl_FragColor = texture2D(Texture, v_tc);\n"
        "}\n";

    GLuint vs = compile_shader(GL_VERTEX_SHADER, ovl_vs, "");
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, ovl_fs, "");
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    ovl_prog = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ovl_prog)
        return 0;

    ovl_a_vtx     = glGetAttribLocation(ovl_prog, "VertexCoord");
    ovl_a_tc      = glGetAttribLocation(ovl_prog, "TexCoord");
    ovl_u_sampler = glGetUniformLocation(ovl_prog, "Texture");

    /* Fullscreen quad: position + texcoord.
       FBO is later flipped by the main blit, so use direct SDL orientation
       (V=0 at top, V=1 at bottom). */
    static const float verts[] = {
        -1.0f, -1.0f, 0, 1,  0.0f, 0.0f, 0, 0,
         1.0f, -1.0f, 0, 1,  1.0f, 0.0f, 0, 0,
        -1.0f,  1.0f, 0, 1,  0.0f, 1.0f, 0, 0,
         1.0f,  1.0f, 0, 1,  1.0f, 1.0f, 0, 0,
    };
    glGenBuffers(1, &ovl_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, ovl_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glGenTextures(1, &ovl_tex);
    glBindTexture(GL_TEXTURE_2D, ovl_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    ovl_inited = 1;
    return 1;
}

void
sdl_shader_draw_overlay(const void *rgba, int w, int h)
{
    if (!is_active || !rgba || w <= 0 || h <= 0)
        return;
    if (!ovl_init())
        return;

    /* Save current program so main shader isn't disturbed. */
    GLint prev_prog;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);

    glUseProgram(ovl_prog);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ovl_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    if (ovl_u_sampler >= 0)
        glUniform1i(ovl_u_sampler, 0);

    glBindBuffer(GL_ARRAY_BUFFER, ovl_vbo);
    if (ovl_a_vtx >= 0) {
        glEnableVertexAttribArray(ovl_a_vtx);
        glVertexAttribPointer(ovl_a_vtx, 4, GL_FLOAT, GL_FALSE,
                              8 * sizeof(float), (void *) 0);
    }
    if (ovl_a_tc >= 0) {
        glEnableVertexAttribArray(ovl_a_tc);
        glVertexAttribPointer(ovl_a_tc, 4, GL_FLOAT, GL_FALSE,
                              8 * sizeof(float), (void *) (4 * sizeof(float)));
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (ovl_a_vtx >= 0) glDisableVertexAttribArray(ovl_a_vtx);
    if (ovl_a_tc >= 0)  glDisableVertexAttribArray(ovl_a_tc);

    glUseProgram(prev_prog);
}

SDL_GLContext
sdl_shader_get_context(void)
{
    return gl_ctx;
}

void
sdl_shader_get_viewport(int *dst_x, int *dst_y, int *dst_w, int *dst_h)
{
    if (dst_x) *dst_x = last_dst_x;
    if (dst_y) *dst_y = last_dst_y;
    if (dst_w) *dst_w = last_dst_w;
    if (dst_h) *dst_h = last_dst_h;
}

/* ------------------------------------------------------------------ */
/*  Passthrough shader (no CRT effect, just blit)                      */
/* ------------------------------------------------------------------ */
static const char *pt_vert_src =
    "attribute vec4 VertexCoord;\n"
    "attribute vec4 TexCoord;\n"
    "varying vec2 v_tc;\n"
    "void main() {\n"
    "    gl_Position = VertexCoord;\n"
    "    v_tc = TexCoord.xy;\n"
    "}\n";

static const char *pt_frag_src =
    "precision mediump float;\n"
    "varying vec2 v_tc;\n"
    "uniform sampler2D Texture;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(Texture, v_tc);\n"
    "}\n";

int
sdl_shader_init_passthrough(SDL_Window *win)
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    gl_ctx = SDL_GL_CreateContext(win);
    if (!gl_ctx) {
        fprintf(stderr, "SDL Shader passthrough: GL context failed: %s\n", SDL_GetError());
        return 0;
    }
    SDL_GL_MakeCurrent(win, gl_ctx);
    SDL_GL_SetSwapInterval(0);

    use_bgra = has_extension("GL_EXT_texture_format_BGRA8888");

    GLuint vs = compile_shader(GL_VERTEX_SHADER, pt_vert_src, "");
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, pt_frag_src, "");
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        SDL_GL_DeleteContext(gl_ctx);
        gl_ctx = NULL;
        return 0;
    }

    prog = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!prog) {
        SDL_GL_DeleteContext(gl_ctx);
        gl_ctx = NULL;
        return 0;
    }

    glUseProgram(prog);

    a_vtx = glGetAttribLocation(prog, "VertexCoord");
    a_tc  = glGetAttribLocation(prog, "TexCoord");

    u_mvp       = -1;
    u_frame_dir = -1;
    u_frame_cnt = -1;
    u_out_size  = -1;
    u_tex_size  = -1;
    u_in_size   = -1;
    u_sampler   = glGetUniformLocation(prog, "Texture");

    static const float verts[] = {
        -1.0f, -1.0f, 0, 1, 0.0f, 1.0f, 0, 0,
         1.0f, -1.0f, 0, 1, 1.0f, 1.0f, 0, 0,
        -1.0f,  1.0f, 0, 1, 0.0f, 0.0f, 0, 0,
         1.0f,  1.0f, 0, 1, 1.0f, 0.0f, 0, 0,
    };
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glGenTextures(1, &fb_tex);
    glBindTexture(GL_TEXTURE_2D, fb_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    tex_w = tex_h = 0;
    frame_count = 0;
    is_active = 1;
    return 1;
}
