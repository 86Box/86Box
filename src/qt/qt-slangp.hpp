#ifndef QT_SLANGP_HPP
#define QT_SLANGP_HPP

#include "librashader.h"

#include <vector>

struct slang_shader
{
    std::string path;
    libra_shader_preset_t shader_preset;

    libra_preset_param_list_t param_list;
    std::vector<double> param_values;
};

typedef struct slang_shader slang_shader;

void slangp_read_shader_config(slang_shader& shader);
void slangp_write_shader_config(slang_shader& shader);
slang_shader* slangp_parse(const char* path);
void slangp_free(slang_shader* shader);
#endif