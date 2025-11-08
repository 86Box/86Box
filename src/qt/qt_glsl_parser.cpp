#include "qt_mainwindow.hpp"
#include <QMessageBox>
#include <QWindow>
#include <QCoreApplication>

extern MainWindow *main_window;

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
extern "C" {
#include <86box/86box.h>
#include <86box/ini.h>
#include <86box/config.h>
#include <86box/qt-glslp-parser.h>
#include <86box/path.h>
#include <86box/plat.h>

extern void    startblit();
extern void    endblit();
extern ssize_t local_getline(char **buf, size_t *bufsiz, FILE *fp);
extern char   *trim(char *str);
}

#define safe_strncpy(a, b, n)       \
    do {                            \
        strncpy((a), (b), (n) - 1); \
        (a)[(n) - 1] = 0;           \
    } while (0)

static inline void *
wx_config_load(const char *path)
{
    ini_t ini = ini_read(path);
    if (ini)
        ini_strip_quotes(ini);
    return (void *) ini;
}

static inline int
wx_config_get_string(void *config, const char *name, char *dst, int size, const char *defVal)
{
    int   res = ini_has_entry(ini_find_or_create_section((ini_t) config, ""), name);
    char *str = ini_get_string((ini_t) config, "", name, (char *) defVal);
    if (size == 0)
        return res;
    if (str != NULL)
        strncpy(dst, str, size - 1);
    else
        dst[0] = 0;
    return res;
}

static inline int
wx_config_get_int(void *config, const char *name, int *dst, int defVal)
{
    int res = ini_has_entry(ini_find_or_create_section((ini_t) config, ""), name);
    *dst    = ini_get_int((ini_t) config, "", name, defVal);
    return res;
}

static inline int
wx_config_get_float(void *config, const char *name, float *dst, float defVal)
{
    int res = ini_has_entry(ini_find_or_create_section((ini_t) config, ""), name);
    *dst    = (float) ini_get_double((ini_t) config, "", name, defVal);
    return res;
}

static inline int
wx_config_get_bool(void *config, const char *name, int *dst, int defVal)
{
    int res = ini_has_entry(ini_find_or_create_section((ini_t) config, ""), name);
    *dst    = !!ini_get_int((ini_t) config, "", name, defVal);
    return res;
}

static inline int
wx_config_has_entry(void *config, const char *name)
{
    return ini_has_entry(ini_find_or_create_section((ini_t) config, ""), name);
}
static inline void
wx_config_free(void *config)
{
    ini_close(config);
};

static int
endswith(const char *str, const char *ext)
{
    const char *p;
    int         elen = strlen(ext);
    int         slen = strlen(str);
    if (slen >= elen) {
        p = &str[slen - elen];
        for (int i = 0; i < elen; ++i) {
            if (tolower(p[i]) != tolower(ext[i]))
                return 0;
        }
        return 1;
    }
    return 0;
}

static int
glsl_detect_bom(const char *fn)
{
    FILE         *fp;
    unsigned char bom[4] = { 0, 0, 0, 0 };

    fp = plat_fopen(fn, "rb");
    if (fp == NULL)
        return 0;
    (void) !fread(bom, 1, 3, fp);
    if (bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF) {
        fclose(fp);
        return 1;
    }
    fclose(fp);
    return 0;
}

static char *
load_file(const char *fn)
{
    int   bom = glsl_detect_bom(fn);
    FILE *fp  = plat_fopen(fn, "rb");
    if (!fp)
        return 0;
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (bom) {
        fsize -= 3;
        fseek(fp, 3, SEEK_SET);
    }

    char *data = (char *) malloc(fsize + 1);

    size_t read_bytes = fread(data, fsize, 1, fp);
    if (read_bytes != 1) {
        fclose(fp);
        free(data);
        return nullptr;
    } else {
        fclose(fp);

        data[fsize] = 0;

        return data;
    }
}

static void
strip_lines(const char *program, const char *starts_with)
{
    /* strip parameters */
    char *ptr = (char *) strstr(program, starts_with);
    while (ptr != nullptr) {
        while (*ptr != '\n' && *ptr != '\0')
            *ptr++ = ' ';
        ptr = (char *) strstr(program, starts_with);
    }
}

static void
strip_parameters(const char *program)
{
    /* strip parameters */
    strip_lines(program, "#pragma parameter");
}

static void
strip_defines(const char *program)
{
    /* strip texture define */
    strip_lines(program, "#define texture");
}

static int
has_parameter(glslp_t *glsl, char *id)
{
    for (int i = 0; i < glsl->num_parameters; ++i)
        if (!strcmp(glsl->parameters[i].id, id))
            return 1;
    return 0;
}

static int
get_parameters(glslp_t *glsl)
{
    struct parameter p;
    for (int i = 0; i < glsl->num_shaders; ++i) {
        size_t         size   = 0;
        char          *line   = NULL;
        struct shader *shader = &glsl->shaders[i];
        int            bom    = glsl_detect_bom(shader->shader_fn);
        FILE          *f      = plat_fopen(shader->shader_fn, "rb");
        if (!f)
            return 0;
        if (bom) {
            fseek(f, 3, SEEK_SET);
        }
        while (local_getline(&line, &size, f) != -1 && glsl->num_parameters < MAX_PARAMETERS) {
            line[strcspn(line, "\r\n")] = '\0';
            trim(line);
            int num = sscanf(line, "#pragma parameter %63s \"%63[^\"]\" %f %f %f %f", p.id, p.description,
                             &p.default_value, &p.min, &p.max, &p.step);
            if (num < 5)
                continue;
            p.id[63]          = 0;
            p.description[63] = 0;

            if (num == 5)
                p.step = 0.1f * (p.max - p.min);

            p.value = p.default_value;

            if (!has_parameter(glsl, p.id)) {
                memcpy(&glsl->parameters[glsl->num_parameters++], &p, sizeof(struct parameter));
                pclog("Read parameter: %s (%s) %f, %f -> %f (%f)\n", p.id, p.description, p.default_value, p.min,
                      p.max, p.step);
            }
        }

        fclose(f);
    }

    return 1;
}

static struct parameter *
get_parameter(glslp_t *glslp, const char *id)
{
    for (int i = 0; i < glslp->num_parameters; ++i) {
        if (!strcmp(glslp->parameters[i].id, id)) {
            return &glslp->parameters[i];
        }
    }
    return 0;
}

static glslp_t *
glsl_parse(const char *f)
{
    glslp_t *glslp        = (glslp_t *) calloc(1, sizeof(glslp_t));
    glslp->num_shaders    = 1;
    struct shader *shader = &glslp->shaders[0];
    strcpy(shader->shader_fn, f);
    shader->shader_program = load_file(f);
    if (!shader->shader_program) {
        QMessageBox::critical((QWidget *) qApp->findChild<QWindow *>(), QObject::tr("GLSL error"), QObject::tr("Could not load shader: %1").arg(shader->shader_fn));
        // wx_simple_messagebox("GLSL error", "Could not load shader %s\n", shader->shader_fn);
        glslp_free(glslp);
        return 0;
    }
    strip_parameters(shader->shader_program);
    strip_defines(shader->shader_program);
    shader->scale_x = shader->scale_y = 1.0f;
    strcpy(shader->scale_type_x, "source");
    strcpy(shader->scale_type_y, "source");
    get_parameters(glslp);
    return glslp;
}

extern "C" {

void
get_glslp_name(const char *f, char *s, int size)
{
    safe_strncpy(s, path_get_filename((char *) f), size);
}

glslp_t *
glslp_parse(const char *f)
{
    int  j;
    int  len;
    int  sublen;
    char s[2049], t[2049], z[2076];

    memset(s, 0, sizeof(s));
    if (endswith(f, ".glsl"))
        return glsl_parse(f);

    void *cfg = wx_config_load(f);

    if (!cfg) {
        fprintf(stderr, "GLSLP Error: Could not load GLSLP-file %s\n", f);
        return 0;
    }

    glslp_t *glslp = (glslp_t *) calloc(1, sizeof(glslp_t));

    get_glslp_name(f, glslp->name, sizeof(glslp->name));

    wx_config_get_int(cfg, "shaders", &glslp->num_shaders, 0);

    wx_config_get_bool(cfg, "filter_linear0", &glslp->input_filter_linear, -1);

    for (int i = 0; i < glslp->num_shaders; ++i) {
        struct shader *shader = &glslp->shaders[i];

        snprintf(s, sizeof(s) - 1, "shader%d", i);
        if (!wx_config_get_string(cfg, s, t, sizeof(t), 0)) {
            /* shader doesn't exist, lets break here */
            glslp->num_shaders = i;
            break;
        }
        strcpy(s, f);
        *path_get_filename(s) = 0;

        size_t max_len = sizeof(shader->shader_fn);
        size_t s_len   = strlen(s);

        if (s_len >= max_len) {
            // s alone fills or overflows the buffer, truncate and null-terminate
            size_t copy_len = max_len - 1 < s_len ? max_len - 1 : s_len;
            memcpy(shader->shader_fn, s, copy_len);
            shader->shader_fn[copy_len] = '\0';
        } else {
            // Copy s fully
            memcpy(shader->shader_fn, s, s_len);
            // Copy as much of t as fits after s
            size_t avail = max_len - 1 - s_len; // space left for t + null terminator
            // Copy as much of t as fits into the remaining space
            memcpy(shader->shader_fn + s_len, t, avail);
            // Null-terminate
            shader->shader_fn[s_len + avail] = '\0';
        }

        shader->shader_program = load_file(shader->shader_fn);
        if (!shader->shader_program) {
            fprintf(stderr, "GLSLP Error: Could not load shader %s\n", shader->shader_fn);
            glslp_free(glslp);
            return 0;
        }
        strip_parameters(shader->shader_program);
        strip_defines(shader->shader_program);

        snprintf(s, sizeof(s) - 1, "alias%d", i);
        wx_config_get_string(cfg, s, shader->alias, sizeof(shader->alias), 0);

        snprintf(s, sizeof(s) - 1, "filter_linear%d", i + 1);
        wx_config_get_bool(cfg, s, &shader->filter_linear, 0);

        snprintf(s, sizeof(s) - 1, "wrap_mode%d", i);
        wx_config_get_string(cfg, s, shader->wrap_mode, sizeof(shader->wrap_mode), 0);

        snprintf(s, sizeof(s) - 1, "float_framebuffer%d", i);
        wx_config_get_bool(cfg, s, &shader->float_framebuffer, 0);
        snprintf(s, sizeof(s) - 1, "srgb_framebuffer%d", i);
        wx_config_get_bool(cfg, s, &shader->srgb_framebuffer, 0);

        snprintf(s, sizeof(s) - 1, "mipmap_input%d", i);
        wx_config_get_bool(cfg, s, &shader->mipmap_input, 0);

        strcpy(shader->scale_type_x, "source");
        snprintf(s, sizeof(s) - 1, "scale_type_x%d", i);
        wx_config_get_string(cfg, s, shader->scale_type_x, sizeof(shader->scale_type_x), 0);
        strcpy(shader->scale_type_y, "source");
        snprintf(s, sizeof(s) - 1, "scale_type_y%d", i);
        wx_config_get_string(cfg, s, shader->scale_type_y, sizeof(shader->scale_type_y), 0);
        snprintf(s, sizeof(s) - 1, "scale_type%d", i);
        if (wx_config_has_entry(cfg, s)) {
            wx_config_get_string(cfg, s, shader->scale_type_x, sizeof(shader->scale_type_x), 0);
            wx_config_get_string(cfg, s, shader->scale_type_y, sizeof(shader->scale_type_y), 0);
        }

        snprintf(s, sizeof(s) - 1, "scale_x%d", i);
        wx_config_get_float(cfg, s, &shader->scale_x, 1.0f);
        snprintf(s, sizeof(s) - 1, "scale_y%d", i);
        wx_config_get_float(cfg, s, &shader->scale_y, 1.0f);
        snprintf(s, sizeof(s) - 1, "scale%d", i);
        if (wx_config_has_entry(cfg, s)) {
            wx_config_get_float(cfg, s, &shader->scale_x, 1.0f);
            wx_config_get_float(cfg, s, &shader->scale_y, 1.0f);
        }

        snprintf(s, sizeof(s) - 1, "frame_count_mod%d", i);
        wx_config_get_int(cfg, s, &shader->frame_count_mod, 0);
    }

    /* textures */
    glslp->num_textures = 0;
    wx_config_get_string(cfg, "textures", t, sizeof(t), 0);

    len    = strlen(t);
    j      = 0;
    sublen = 0;
    for (int i = 0; i < len; ++i) {
        if (t[i] == ';' || i == len - 1) {
            sublen = (i - j) + ((i == len - 1) ? 1 : 0) + 1;
            safe_strncpy(s, t + j, sublen);
            s[511 < sublen ? 511 : sublen] = 0;

            if (s[strlen(s) - 1] == ';')
                s[strlen(s) - 1] = 0;

            struct texture *tex = &glslp->textures[glslp->num_textures++];

            strcpy(tex->name, s);
            wx_config_get_string(cfg, s, tex->path, sizeof(tex->path), 0);

            snprintf(z, sizeof(z) - 1, "%s_linear", s);
            wx_config_get_bool(cfg, z, &tex->linear, 0);

            snprintf(z, sizeof(z) - 1, "%s_mipmap", s);
            wx_config_get_bool(cfg, z, &tex->mipmap, 0);

            snprintf(z, sizeof(z) - 1, "%s_wrap_mode", s);
            wx_config_get_string(cfg, z, tex->wrap_mode, sizeof(tex->wrap_mode), 0);

            j = i + 1;
        }
    }

    /* parameters */
    get_parameters(glslp);

    wx_config_get_string(cfg, "parameters", t, sizeof(t), 0);

    len    = strlen(t);
    j      = 0;
    sublen = 0;
    for (int i = 0; i < len; ++i) {
        if (t[i] == ';' || i == len - 1) {
            sublen = (i - j) + ((i == len - 1) ? 1 : 0) + 1;
            safe_strncpy(s, t + j, sublen);
            s[511 < sublen ? 511 : sublen] = 0;

            struct parameter *p = get_parameter(glslp, s);

            if (p)
                wx_config_get_float(cfg, s, &p->default_value, 0);

            j = i + 1;
        }
    }

    wx_config_free(cfg);

    return glslp;
}

void
glslp_free(glslp_t *p)
{
    for (int i = 0; i < p->num_shaders; ++i)
        if (p->shaders[i].shader_program)
            free(p->shaders[i].shader_program);
    free(p);
}

void
glslp_read_shader_config(glslp_t *shader)
{
    char  s[512];
    char *name = shader->name;
    snprintf(s, sizeof(s) - 1, "GL3 Shaders - %s", name);
    for (int i = 0; i < shader->num_parameters; ++i) {
        struct parameter *param = &shader->parameters[i];
        param->value            = config_get_double(s, param->id, param->default_value);
    }
}

void
glslp_write_shader_config(glslp_t *shader)
{
    char  s[512];
    char *name = shader->name;

    startblit();
    snprintf(s, sizeof(s) - 1, "GL3 Shaders - %s", name);
    for (int i = 0; i < shader->num_parameters; ++i) {
        struct parameter *param = &shader->parameters[i];
        config_set_double(s, param->id, param->value);
    }
    endblit();
}
}
