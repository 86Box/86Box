/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		File parser for .glslp and .glsl shader files
 *		in the format of libretro.
 *
 * TODO:	Read .glslp files for multipass shaders and settings.
 *
 * Authors:	Teemu Korhonen
 *
 *		Copyright 2021 Teemu Korhonen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <glad/glad.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/win_opengl_glslp.h>

 /**
  * @brief Default vertex shader.
 */
static const GLchar* vertex_shader = "#version 330 core\n\
in vec2 VertexCoord;\n\
in vec2 TexCoord;\n\
out vec2 tex;\n\
void main(){\n\
	gl_Position = vec4(VertexCoord, 0.0, 1.0);\n\
	tex = TexCoord;\n\
}\n";

/**
 * @brief Default fragment shader.
 */
static const GLchar* fragment_shader = "#version 330 core\n\
in vec2 tex;\n\
uniform sampler2D texsampler;\n\
void main() {\n\
	gl_FragColor = texture(texsampler, tex);\n\
}\n";

/**
 * @brief Reads a whole file into a null terminated string.
 * @param Path Path to the file relative to executable path.
 * @return Pointer to the string or NULL on error. Remember to free() after use.
*/
static char* read_file_to_string(const char* path)
{
	char* full_path = (char*)malloc(sizeof(char) * (strlen(path) + strlen(exe_path) + 1));

	plat_append_filename(full_path, exe_path, path);

	FILE* file_handle = plat_fopen(full_path, "rb");

	free(full_path);

	if (file_handle != NULL)
	{
		/* get file size */
		fseek(file_handle, 0, SEEK_END);
		
		size_t file_size = (size_t)ftell(file_handle);
		
		fseek(file_handle, 0, SEEK_SET);

		/* read to buffer and close */
		char* content = (char*)malloc(sizeof(char) * (file_size + 1));

		if (!content)
			return NULL;

		size_t length = fread(content, sizeof(char), file_size, file_handle);

		fclose(file_handle);

		content[length] = 0;

		return content;
	}
	return NULL;
}

/**
 * @brief Compile custom shaders into a program.
 * @return Shader program identifier.
*/
GLuint load_custom_shaders(const char* path)
{
	GLint status = GL_FALSE;
	int info_log_length;

	/* TODO: get path from config */
	char* shader = read_file_to_string(path);

	if (shader != NULL)
	{
		int success = 1;

		const char* vertex_sources[2] = { "#define VERTEX\n", shader };
		const char* fragment_sources[2] = { "#define FRAGMENT\n", shader };

		GLuint vertex_id = glCreateShader(GL_VERTEX_SHADER);
		GLuint fragment_id = glCreateShader(GL_FRAGMENT_SHADER);

		glShaderSource(vertex_id, 2, vertex_sources, NULL);
		glCompileShader(vertex_id);
		glGetShaderiv(vertex_id, GL_COMPILE_STATUS, &status);

		if (status == GL_FALSE)
		{
			glGetShaderiv(vertex_id, GL_INFO_LOG_LENGTH, &info_log_length);

			GLchar* info_log_text = (GLchar*)malloc(sizeof(GLchar) * info_log_length);

			glGetShaderInfoLog(vertex_id, info_log_length, NULL, info_log_text);

			/* TODO: error logging */

			free(info_log_text);

			success = 0;
		}

		glShaderSource(fragment_id, 2, fragment_sources, NULL);
		glCompileShader(fragment_id);
		glGetShaderiv(fragment_id, GL_COMPILE_STATUS, &status);

		if (status == GL_FALSE)
		{
			glGetShaderiv(fragment_id, GL_INFO_LOG_LENGTH, &info_log_length);

			GLchar* info_log_text = (GLchar*)malloc(sizeof(GLchar) * info_log_length);

			glGetShaderInfoLog(fragment_id, info_log_length, NULL, info_log_text);

			/* TODO: error logging */

			free(info_log_text);

			success = 0;
		}

		free(shader);

		GLuint prog_id = 0;

		if (success)
		{
			prog_id = glCreateProgram();

			glAttachShader(prog_id, vertex_id);
			glAttachShader(prog_id, fragment_id);
			glLinkProgram(prog_id);
			glGetProgramiv(prog_id, GL_LINK_STATUS, &status);

			if (status == GL_FALSE)
			{
				glGetProgramiv(prog_id, GL_INFO_LOG_LENGTH, &info_log_length);

				GLchar* info_log_text = (GLchar*)malloc(sizeof(GLchar) * info_log_length);

				glGetProgramInfoLog(prog_id, info_log_length, NULL, info_log_text);

				/* TODO: error logging */

				free(info_log_text);
			}

			glDetachShader(prog_id, vertex_id);
			glDetachShader(prog_id, fragment_id);
		}

		glDeleteShader(vertex_id);
		glDeleteShader(fragment_id);

		return prog_id;
	}
	return 0;
}

/**
 * @brief Compile default shaders into a program.
 * @return Shader program identifier.
*/
GLuint load_default_shaders()
{
	GLuint vertex_id = glCreateShader(GL_VERTEX_SHADER);
	GLuint fragment_id = glCreateShader(GL_FRAGMENT_SHADER);

	glShaderSource(vertex_id, 1, &vertex_shader, NULL);
	glCompileShader(vertex_id);

	glShaderSource(fragment_id, 1, &fragment_shader, NULL);
	glCompileShader(fragment_id);

	GLuint prog_id = glCreateProgram();

	glAttachShader(prog_id, vertex_id);
	glAttachShader(prog_id, fragment_id);

	glLinkProgram(prog_id);

	glDetachShader(prog_id, vertex_id);
	glDetachShader(prog_id, fragment_id);

	glDeleteShader(vertex_id);
	glDeleteShader(fragment_id);

	return prog_id;
}