/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Rendering module for OpenGL
 * 
 * NOTE:	This is very much still a work-in-progress.
 *		Expect missing functionality, hangs and bugs.
 * 
 * TODO:	Loader for glsl files.
 *			libretro has a sizeable library that could
 *			be a good target for compatibility.
 *		(UI) options
 *		More error handling
 *		...
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

#define UNICODE
#include <Windows.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <glad/glad.h>

#include <stdio.h>

#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/win.h>
#include <86box/win_opengl.h>

static const int INIT_WIDTH = 640;
static const int INIT_HEIGHT = 400;

/**
 * @brief A dedicated OpenGL thread.
 * OpenGL context's don't handle multiple threads well.
*/
static thread_t* thread = NULL;

/**
 * @brief A window usable with an OpenGL context
*/
static SDL_Window* window = NULL;

/**
 * @brief SDL window handle
*/
static HWND window_hwnd = NULL;

/**
 * @brief Parent window handle (hwndRender from win_ui)
*/
static HWND parent = NULL;

/**
 * @brief Events listened in OpenGL thread.
*/
static union
{
	struct
	{
		HANDLE closing;
		HANDLE resize;
		HANDLE blit_waiting;
	};
	HANDLE asArray[3];
} sync_objects = {};

/**
 * @brief Signal from OpenGL thread that it's done with video buffer.
*/
static HANDLE blit_done = NULL;

/**
 * @brief Blit event parameters.
*/
static volatile struct
{
	int x, y, y1, y2, w, h, resized;
} blit_info = {};

/**
 * @brief Resize event parameters.
*/
static volatile struct
{
	int width, height, fullscreen, scaling_mode;
} resize_info = {};

/**
 * @brief Identifiers to OpenGL objects and uniforms.
 */
typedef struct
{
	GLuint vertexArrayID;
	GLuint vertexBufferID;
	GLuint textureID;
	GLuint shader_progID;

	/* Uniforms */

	GLint input_size;
	GLint output_size;
	GLint texture_size;
	GLint frame_count;
} gl_identifiers;

/**
 * @brief Userdata to pass onto windows message hook
*/
typedef struct
{
	HWND window;
	int* fullscreen;
} winmessage_data;

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
		long file_size = ftell(file_handle);
		fseek(file_handle, 0, SEEK_SET);

		/* read to buffer and close */
		char* content = (char*)malloc(sizeof(char) * (file_size + 1));
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
static GLuint load_custom_shaders()
{
	GLint status = GL_FALSE;
	int info_log_length;

	/* TODO: get path from config */
	char* shader = read_file_to_string("shaders/shader.glsl");

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
static GLuint load_default_shaders()
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

/**
 * @brief Set or unset OpenGL context window as a child window.
 * 
 * Modifies the window style and sets the parent window.
 * WS_EX_NOACTIVATE keeps the window from stealing input focus.
 */
static void set_parent_binding(int enable)
{
	long style = GetWindowLong(window_hwnd, GWL_STYLE);
	long ex_style = GetWindowLong(window_hwnd, GWL_EXSTYLE);

	if (enable)
	{
		style |= WS_CHILD;
		ex_style |= WS_EX_NOACTIVATE;
	}
	else
	{
		style &= ~WS_CHILD;
		ex_style &= ~WS_EX_NOACTIVATE;
	}

	SetWindowLong(window_hwnd, GWL_STYLE, style);
	SetWindowLong(window_hwnd, GWL_EXSTYLE, ex_style);

	SetParent(window_hwnd, enable ? parent : NULL);
}

/**
 * @brief Windows message handler for SDL Windows.
 * @param userdata winmessage_data
 * @param hWnd
 * @param message
 * @param wParam
 * @param lParam 
*/
static void winmessage_hook(void* userdata, void* hWnd, unsigned int message, Uint64 wParam, Sint64 lParam)
{
	winmessage_data* msg_data = (winmessage_data*)userdata;

	/* Process only our window */
	if (msg_data->window != hWnd || parent == NULL)
		return;

	switch (message)
	{
	case WM_LBUTTONUP:
	case WM_LBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MBUTTONDOWN:
		if (!*msg_data->fullscreen)
		{
			/* Mouse events that enter and exit capture. */
			PostMessage(parent, message, wParam, lParam);
		}
		break;
	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
		if (*msg_data->fullscreen)
		{
			PostMessage(parent, message, wParam, lParam);
		}
		break;
	case WM_INPUT:
		if (*msg_data->fullscreen)
		{
			/* Raw input handler from win_ui.c : input_proc */

			UINT size = 0;
			PRAWINPUT raw = NULL;

			/* Here we read the raw input data */
			GetRawInputData((HRAWINPUT)(LPARAM)lParam, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER));
			raw = (PRAWINPUT)malloc(size);
			if (GetRawInputData((HRAWINPUT)(LPARAM)lParam, RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER)) == size) {
				switch (raw->header.dwType)
				{
				case RIM_TYPEKEYBOARD:
					keyboard_handle(raw);
					break;
				case RIM_TYPEMOUSE:
					win_mouse_handle(raw);
					break;
				case RIM_TYPEHID:
					win_joystick_handle(raw);
					break;
				}
			}
			free(raw);
		}
		break;
	}
}

/**
 * @brief Initialize OpenGL context
 * @return Object identifiers
*/
static gl_identifiers initialize_glcontext()
{
	/* Vertex, texture 2d coordinates and color (white) making a quad as triangle strip */
	static const GLfloat surface[] = {
		-1.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f,
		1.f, 1.f, 1.f, 0.f, 1.f, 1.f, 1.f, 1.f,
		-1.f, -1.f, 0.f, 1.f, 1.f, 1.f, 1.f, 1.f,
		1.f, -1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f
	};

	gl_identifiers gl = {};

	glGenVertexArrays(1, &gl.vertexArrayID);

	glBindVertexArray(gl.vertexArrayID);

	glGenBuffers(1, &gl.vertexBufferID);
	glBindBuffer(GL_ARRAY_BUFFER, gl.vertexBufferID);
	glBufferData(GL_ARRAY_BUFFER, sizeof(surface), surface, GL_STATIC_DRAW);

	glGenTextures(1, &gl.textureID);
	glBindTexture(GL_TEXTURE_2D, gl.textureID);

	GLfloat border_color[] = { 0.f, 0.f, 0.f, 1.f };
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border_color);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);

	glClearColor(0.f, 0.f, 0.f, 1.f);

	gl.shader_progID = load_custom_shaders();

	if (gl.shader_progID == 0)
		gl.shader_progID = load_default_shaders();

	glUseProgram(gl.shader_progID);

	GLint vertex_coord = glGetAttribLocation(gl.shader_progID, "VertexCoord");
	if (vertex_coord != -1)
	{
		glEnableVertexAttribArray(vertex_coord);
		glVertexAttribPointer(vertex_coord, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), 0);
	}

	GLint tex_coord = glGetAttribLocation(gl.shader_progID, "TexCoord");
	if (tex_coord != -1)
	{
		glEnableVertexAttribArray(tex_coord);
		glVertexAttribPointer(tex_coord, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));
	}

	GLint color = glGetAttribLocation(gl.shader_progID, "Color");
	if (color != -1)
	{
		glEnableVertexAttribArray(color);
		glVertexAttribPointer(color, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void*)(4 * sizeof(GLfloat)));
	}

	GLint mvp_matrix = glGetUniformLocation(gl.shader_progID, "MVPMatrix");
	if (mvp_matrix != -1)
	{
		static const GLfloat mvp[] = {
			1.f, 0.f, 0.f, 0.f,
			0.f, 1.f, 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f,
			0.f, 0.f, 0.f, 1.f
		};
		glUniformMatrix4fv(mvp_matrix, 1, GL_FALSE, mvp);
	}

	gl.input_size = glGetUniformLocation(gl.shader_progID, "InputSize");
	gl.output_size = glGetUniformLocation(gl.shader_progID, "OutputSize");
	gl.texture_size = glGetUniformLocation(gl.shader_progID, "TextureSize");
	gl.frame_count = glGetUniformLocation(gl.shader_progID, "FrameCount");

	return gl;
}

/**
 * @brief Clean up OpenGL context 
 * @param Object identifiers from initialize 
*/
static void finalize_glcontext(gl_identifiers obj)
{
	glDeleteProgram(obj.shader_progID);
	glDeleteTextures(1, &obj.textureID);
	glDeleteBuffers(1, &obj.vertexBufferID);
	glDeleteVertexArrays(1, &obj.vertexArrayID);
}

/**
 * @brief Main OpenGL thread proc.
 * 
 * OpenGL context should be accessed only from this single thread.
 * Events are used to synchronize communication.
*/
static void opengl_main()
{
	SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1"); /* Is this actually doing anything...? */

	window = SDL_CreateWindow("86Box OpenGL Renderer", 0, 0, resize_info.width, resize_info.height, SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS);

	/* Keep track of certain parameters, only changed in this thread to avoid race conditions */
	int fullscreen = resize_info.fullscreen, video_width = INIT_WIDTH, video_height = INIT_HEIGHT;

	SDL_SysWMinfo wmi;
	SDL_VERSION(&wmi.version);
	SDL_GetWindowWMInfo(window, &wmi);

	if (wmi.subsystem == SDL_SYSWM_WINDOWS)
		window_hwnd = wmi.info.win.window;

	/* Pass window handle and full screen mode to windows message hook */
	winmessage_data msg_data = (winmessage_data){ window_hwnd, &fullscreen };
	
	SDL_SetWindowsMessageHook(winmessage_hook, &msg_data);

	if (!fullscreen)
		set_parent_binding(1);
	else
		SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);

	SDL_GLContext context = SDL_GL_CreateContext(window);

	gladLoadGLLoader(SDL_GL_GetProcAddress);

	gl_identifiers gl = initialize_glcontext();

	int frame_counter = 0;

	if (gl.frame_count != -1)
		glUniform1i(gl.frame_count, 0);

	/* Render loop */
	int closing = 0;
	while (!closing)
	{
		/* Now redrawing only after a blit. For some shaders to work properly redraw should be in sync with (emulated) display refresh rate. */

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		
		SDL_GL_SwapWindow(window);

		DWORD wait_result = WAIT_TIMEOUT;

		do
		{
			SDL_Event event;

			/* Handle SDL_Window events */
			while (SDL_PollEvent(&event)) { /* No need for actual handlers, but message queue must be processed. */ }

			/* Keep cursor hidden in full screen and mouse capture */
			int show_cursor = !(fullscreen || !!mouse_capture);
			if (SDL_ShowCursor(-1) != show_cursor)
				SDL_ShowCursor(show_cursor);

			/* Wait for synchronized events for 1ms before going back to window events */
			wait_result = WaitForMultipleObjects(sizeof(sync_objects) / sizeof(HANDLE), sync_objects.asArray, FALSE, 1);

		} while (wait_result == WAIT_TIMEOUT);

		HANDLE sync_event = sync_objects.asArray[wait_result - WAIT_OBJECT_0];

		if (sync_event == sync_objects.closing)
		{
			closing = 1;
		}
		else if (sync_event == sync_objects.blit_waiting)
		{
			/* Resize the texture */
			if (blit_info.resized)
			{
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, blit_info.w, blit_info.h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);

				video_width = blit_info.w;
				video_height = blit_info.h;

				if (fullscreen)
					SetEvent(sync_objects.resize);
			}

			/* Transfer video buffer to texture */
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, blit_info.y1, blit_info.w, blit_info.y2 - blit_info.y1, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, &(render_buffer->dat)[blit_info.y1 * blit_info.w]);

			/* Signal that we're done with the video buffer */
			SetEvent(blit_done);

			/* Update uniforms */
			if (gl.input_size != -1)
				glUniform2f(gl.input_size, video_width, video_height);
			if (gl.texture_size != -1)
				glUniform2f(gl.texture_size, video_width, video_height);
			if (gl.frame_count != -1)
				glUniform1i(gl.frame_count, frame_counter = (frame_counter + 1) & 1023);
		}
		else if (sync_event == sync_objects.resize)
		{
			if (fullscreen != resize_info.fullscreen)
			{
				fullscreen = resize_info.fullscreen;

				set_parent_binding(!fullscreen);

				SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);

				if (fullscreen)
				{
					SetForegroundWindow(window_hwnd);
					SetFocus(window_hwnd);
				}
			}

			if (fullscreen)
			{
				int width, height, pad_x = 0, pad_y = 0, px_size = 1;
				float ratio = 0;
				const float ratio43 = 4.f / 3.f;
				
				SDL_GetWindowSize(window, &width, &height);

				if (video_width > 0 && video_height > 0)
				{
					switch (resize_info.scaling_mode)
					{
					case FULLSCR_SCALE_INT:
						px_size = max(min(width / video_width, height / video_height), 1);

						pad_x = width - (video_width * px_size);
						pad_y = height - (video_height * px_size);
						break;

					case FULLSCR_SCALE_KEEPRATIO:
						ratio = (float)video_width / (float)video_height;
					case FULLSCR_SCALE_43:
						if (ratio == 0)
							ratio = ratio43;
						if (ratio < ((float)width / (float)height))
							pad_x = width - (int)roundf((float)height * ratio);
						else
							pad_y = height - (int)roundf((float)width / ratio);
						break;

					case FULLSCR_SCALE_FULL:
					default:
						break;
					}
				}

				glViewport(pad_x / 2, pad_y / 2, width - pad_x, height - pad_y);

				if (gl.output_size != -1)
					glUniform2f(gl.output_size, width - pad_x, height - pad_y);
			}
			else
			{
				SDL_SetWindowSize(window, resize_info.width, resize_info.height);

				/* SWP_NOZORDER is needed for child window and SDL doesn't enable it. */
				SetWindowPos(window_hwnd, parent, 0, 0, resize_info.width, resize_info.height, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOACTIVATE);

				glViewport(0, 0, resize_info.width, resize_info.height);

				if (gl.output_size != -1)
					glUniform2f(gl.output_size, resize_info.width, resize_info.height);
			}
		}
	}

	finalize_glcontext(gl);

	SDL_GL_DeleteContext(context);

	set_parent_binding(0);

	SDL_SetWindowsMessageHook(NULL, NULL);

	SDL_DestroyWindow(window);

	window = NULL;
}

static void opengl_blit(int x, int y, int y1, int y2, int w, int h)
{
	if (y1 == y2 || h <= 0 || render_buffer == NULL || thread == NULL)
	{
		video_blit_complete();
		return;
	}

	blit_info.resized = (w != blit_info.w || h != blit_info.h);
	blit_info.x = x;
	blit_info.y = y;
	blit_info.y1 = y1;
	blit_info.y2 = y2;
	blit_info.w = w;
	blit_info.h = h;

	SignalObjectAndWait(sync_objects.blit_waiting, blit_done, INFINITE, FALSE);

	video_blit_complete();
}

int opengl_init(HWND hwnd)
{
	for (int i = 0; i < sizeof(sync_objects) / sizeof(HANDLE); i++)
		sync_objects.asArray[i] = CreateEvent(NULL, FALSE, FALSE, NULL);

	blit_done = CreateEvent(NULL, FALSE, FALSE, NULL);

	parent = hwnd;
	
	RECT parent_size;

	GetWindowRect(parent, &parent_size);

	resize_info.width = parent_size.right - parent_size.left;
	resize_info.height = parent_size.bottom - parent_size.top;
	resize_info.fullscreen = video_fullscreen & 1;
	resize_info.scaling_mode = video_fullscreen_scale;

	thread = thread_create(opengl_main, (void*)NULL);

	atexit(opengl_close);

	video_setblit(opengl_blit);

	return 1;
}

int opengl_pause()
{
	return 0;
}

void opengl_close()
{
	if (thread == NULL)
		return;

	SetEvent(sync_objects.closing);

	thread_wait(thread, -1);

	memset((void*)&blit_info, 0, sizeof(blit_info));

	SetEvent(blit_done);

	thread = NULL;

	CloseHandle(blit_done);

	for (int i = 0; i < sizeof(sync_objects) / sizeof(HANDLE); i++)
	{
		CloseHandle(sync_objects.asArray[i]);
		sync_objects.asArray[i] = (HANDLE)NULL;
	}

	parent = NULL;
}

void opengl_set_fs(int fs)
{
	if (thread == NULL)
		return;

	resize_info.fullscreen = fs;
	resize_info.scaling_mode = video_fullscreen_scale;

	SetEvent(sync_objects.resize);
}

void opengl_resize(int w, int h)
{
	if (thread == NULL)
		return;

	resize_info.width = w;
	resize_info.height = h;
	resize_info.scaling_mode = video_fullscreen_scale;

	SetEvent(sync_objects.resize);
}