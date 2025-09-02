#ifndef SRC_WX_GLSLP_PARSER_H_
#define SRC_WX_GLSLP_PARSER_H_

#include "qt-glsl.h"

struct parameter {
        char id[64];
        char description[64];
        float default_value;
        float value;
        float min;
        float max;
        float step;
};

struct texture {
        char path[256];
        char name[50];
        int linear;
        int mipmap;
        char wrap_mode[50];
};

struct shader {
        char shader_fn[1024];
        char *shader_program;
        char alias[64];
        int filter_linear;
        int float_framebuffer;
        int srgb_framebuffer;
        int mipmap_input;
        int frame_count_mod;
        char wrap_mode[50];
        char scale_type_x[9], scale_type_y[9];
        float scale_x, scale_y;
};

typedef struct glslp_t {
        char name[64];
        int num_shaders;
        struct shader shaders[MAX_SHADERS];

        int num_textures;
        struct texture textures[MAX_TEXTURES];

        int num_parameters;
        struct parameter parameters[MAX_PARAMETERS];

        int input_filter_linear;
} glslp_t;

void get_glslp_name(const char *f, char *s, int size);
glslp_t *glslp_parse(const char *f);
void glslp_free(glslp_t *p);

void glslp_read_shader_config(glslp_t *shader);
void glslp_write_shader_config(glslp_t *shader);

#endif /* SRC_WX_GLSLP_PARSER_H_ */
