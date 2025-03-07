#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
extern "C"
{
#include <86box/86box.h>
#include <86box/ini.h>
#include <86box/qt-glslp-parser.h>
#include <86box/path.h>
}

#define safe_strncpy(a, b, n)                                                                                                    \
        do {                                                                                                                     \
                strncpy((a), (b), (n)-1);                                                                                        \
                (a)[(n)-1] = 0;                                                                                                  \
        } while (0)

static int endswith(const char *str, const char *ext) {
        int i;
        const char *p;
        int elen = strlen(ext);
        int slen = strlen(str);
        if (slen >= elen) {
                p = &str[slen - elen];
                for (i = 0; i < elen; ++i) {
                        if (tolower(p[i]) != tolower(ext[i]))
                                return 0;
                }
                return 1;
        }
        return 0;
}

static char *load_file(const char *fn) {
        FILE *f = fopen(fn, "rb");
        if (!f)
                return 0;
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        char *data = (char*)malloc(fsize + 1);

        fread(data, fsize, 1, f);
        fclose(f);

        data[fsize] = 0;

        return data;
}

static void strip_lines(const char *program, const char *starts_with) {
        /* strip parameters */
        char *ptr = strstr(program, starts_with);
        while (ptr) {
                while (*ptr != '\n' && *ptr != '\0')
                        *ptr++ = ' ';
                ptr = strstr(program, starts_with);
        }
}

static void strip_parameters(const char *program) {
        /* strip parameters */
        strip_lines(program, "#pragma parameter");
}

static void strip_defines(const char *program) {
        /* strip texture define */
        strip_lines(program, "#define texture");
}

static int has_parameter(glslp_t *glsl, char *id) {
        int i;
        for (i = 0; i < glsl->num_parameters; ++i)
                if (!strcmp(glsl->parameters[i].id, id))
                        return 1;
        return 0;
}

static int get_parameters(glslp_t *glsl) {
        int i;
        struct parameter p;
        for (i = 0; i < glsl->num_shaders; ++i) {
                struct shader *shader = &glsl->shaders[i];
                FILE *f = fopen(shader->shader_fn, "rb");
                if (!f)
                        return 0;

                char line[1024];
                while (fgets(line, sizeof(line) - 1, f) && glsl->num_parameters < MAX_PARAMETERS) {
                        int num = sscanf(line, "#pragma parameter %63s \"%63[^\"]\" %f %f %f %f", p.id, p.description,
                                         &p.default_value, &p.min, &p.max, &p.step);
                        if (num < 5)
                                continue;
                        p.id[63] = 0;
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

static struct parameter *get_parameter(glslp_t *glslp, const char *id) {
        int i;
        for (i = 0; i < glslp->num_parameters; ++i) {
                if (!strcmp(glslp->parameters[i].id, id)) {
                        return &glslp->parameters[i];
                }
        }
        return 0;
}

static glslp_t *glsl_parse(const char *f) {
        glslp_t *glslp = (glslp_t*)malloc(sizeof(glslp_t));
        memset(glslp, 0, sizeof(glslp_t));
        glslp->num_shaders = 1;
        struct shader *shader = &glslp->shaders[0];
        strcpy(shader->shader_fn, f);
        shader->shader_program = load_file(f);
        if (!shader->shader_program) {
                //wx_simple_messagebox("GLSL error", "Could not load shader %s\n", shader->shader_fn);
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

void get_glslp_name(const char *f, char *s, int size) { safe_strncpy(s, path_get_filename((char *)f), size); }

glslp_t *glslp_parse(const char *f) {
        int i, j, len, sublen;
        char s[513], t[513], z[540];

        memset(s, 0, sizeof(s));
        if (endswith(f, ".glsl"))
                return glsl_parse(f);

        void *cfg = wx_config_load(f);

        if (!cfg) {
                fprintf(stderr, "GLSLP Error: Could not load GLSLP-file %s\n", f);
                return 0;
        }

        glslp_t *glslp = (glslp_t*)malloc(sizeof(glslp_t));
        memset(glslp, 0, sizeof(glslp_t));

        get_glslp_name(f, glslp->name, sizeof(glslp->name));

        wx_config_get_int(cfg, "shaders", &glslp->num_shaders, 0);

        wx_config_get_bool(cfg, "filter_linear0", &glslp->input_filter_linear, -1);

        for (i = 0; i < glslp->num_shaders; ++i) {
                struct shader *shader = &glslp->shaders[i];

                snprintf(s, sizeof(s) - 1, "shader%d", i);
                if (!wx_config_get_string(cfg, s, t, sizeof(t), 0)) {
                        /* shader doesn't exist, lets break here */
                        glslp->num_shaders = i;
                        break;
                }
                strcpy(s, f);
                *path_get_filename(s) = 0;
                snprintf(shader->shader_fn, sizeof(shader->shader_fn) - 1, "%s%s", s, t);
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

        len = strlen(t);
        j = 0;
        sublen = 0;
        for (i = 0; i < len; ++i) {
                if (t[i] == ';' || i == len - 1) {
                        sublen = (i - j) + ((i == len - 1) ? 1 : 0) + 1;
                        safe_strncpy(s, t + j, sublen);
                        s[511 < sublen ? 511 : sublen] = 0;

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

        len = strlen(t);
        j = 0;
        sublen = 0;
        for (i = 0; i < len; ++i) {
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

void glslp_free(glslp_t *p) {
        int i;
        for (i = 0; i < p->num_shaders; ++i)
                if (p->shaders[i].shader_program)
                        free(p->shaders[i].shader_program);
        free(p);
}

}