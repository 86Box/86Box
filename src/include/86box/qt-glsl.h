#ifndef SRC_WX_GLSL_H_
#define SRC_WX_GLSL_H_

#define MAX_PREV 7

#define MAX_SHADERS 20
#define MAX_TEXTURES 20
#define MAX_PARAMETERS 100

#define MAX_USER_SHADERS 20
//#define SDL2_SHADER_DEBUG

struct shader_scale {
        int mode[2];
        float value[2];
};

struct shader_state {
        float input_size[2];
        float input_texture_size[2];
        float output_texture_size[2];
        float output_size[2];
        float tex_coords[8];
};

struct shader_vbo {
        int vertex_coord;
        int tex_coord;
        int color;
};

struct shader_texture {
        int id;
        int width;
        int height;
        int type;
        int internal_format;
        int format;
        int min_filter;
        int mag_filter;
        int wrap_mode;
        void *data;
        int mipmap;
};

struct shader_lut_texture {
        char name[50];
        struct shader_texture texture;
};

struct shader_fbo {
        int id;
        struct shader_texture texture;
        int srgb;
        int mipmap_input;
};

struct shader_prev {
        struct shader_fbo fbo;
        struct shader_vbo vbo;
};

struct shader_input {
        int texture;
        int input_size;
        int texture_size;
        int tex_coord;
};

struct shader_uniforms {
        int mvp_matrix;
        int vertex_coord;
        int tex_coord;
        int color;

        int texture;
        int input_size;
        int texture_size;
        int output_size;

        int frame_count;
        int frame_direction;

        struct shader_input orig;
        struct shader_input pass[MAX_SHADERS];
        struct shader_input prev_pass[MAX_SHADERS];
        struct shader_input prev[MAX_PREV];

        int parameters[MAX_PARAMETERS];
        int lut_textures[MAX_TEXTURES];
};

struct shader_program {
        int vertex_shader;
        int fragment_shader;
        int id;
};

struct shader_parameter {
        char id[64];
        char description[64];
        float default_value;
        float value;
        float min;
        float max;
        float step;
};

struct shader_pass {
        int active;
        char alias[64];
        int vertex_array;
        int frame_count_mod;
        struct shader_program program;
        struct shader_uniforms uniforms;
        struct shader_fbo fbo;
        struct shader_vbo vbo;
        struct shader_state state;
        struct shader_scale scale;
};

struct glsl_shader {
        int active;
        char name[64];

        int num_passes;
        struct shader_pass passes[MAX_SHADERS];

        int num_lut_textures;
        struct shader_lut_texture lut_textures[MAX_TEXTURES];

        int num_parameters;
        struct shader_parameter parameters[MAX_PARAMETERS];

        struct shader_pass prev_scene;
        struct shader_prev prev[MAX_PREV + 1];

        int last_prev_update;
        int has_prev;

        float shader_refresh_rate;

        int input_filter_linear;
};

typedef struct glsl_t {
        int num_shaders;
        struct glsl_shader shaders[MAX_USER_SHADERS];
        struct shader_pass scene;
        struct shader_pass final_pass;
        struct shader_pass fs_color;
#ifdef SDL2_SHADER_DEBUG
        struct shader_pass debug;
#endif
        int srgb;
} glsl_t;

#endif