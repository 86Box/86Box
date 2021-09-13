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
 * TODO:	More shader features
 *			- scaling
 *			- multipass
 *			- previous frames 
 *		(UI) options
 *		More error handling
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
#include <process.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <glad/glad.h>

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#if !defined(_MSC_VER) || defined(__clang__)
#include <stdatomic.h>
#else
typedef LONG atomic_flag;
#define atomic_flag_clear(OBJ) InterlockedExchange(OBJ, 0)
#define atomic_flag_test_and_set(OBJ) InterlockedExchange(OBJ, 1)
#endif

#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/win.h>
#include <86box/win_opengl.h>
#include <86box/win_opengl_glslp.h>

static const int INIT_WIDTH = 640;
static const int INIT_HEIGHT = 400;
static const int BUFFERPIXELS = 4460544;	/* Same size as render_buffer, pow(2048+64,2). */
static const int BUFFERBYTES = 17842176;	/* Pixel is 4 bytes. */
static const int BUFFERCOUNT = 3;		/* How many buffers to use for pixel transfer (2-3 is commonly recommended). */

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
		HANDLE reload;
		HANDLE blit_waiting;
	};
	HANDLE asArray[4];
} sync_objects = { 0 };

/**
 * @brief Blit event parameters.
*/
typedef struct
{
	int y1, y2, w, h;
	void* buffer;			/* Buffer for pixel transfer, allocated by gpu driver. */
	volatile atomic_flag in_use;	/* Is buffer currently in use. */
	GLsync sync;			/* Fence sync object used by opengl thread to track pixel transfer completion. */
} blit_info_t;

/**
 * @brief Array of blit_infos, one for each buffer.
*/
static blit_info_t* blit_info = NULL;

/**
 * @brief Buffer index of next write operation.
*/
static int write_pos = 0;

/**
 * @brief Resize event parameters.
*/
static struct
{
	int width, height, fullscreen, scaling_mode;
	mutex_t* mutex;
} resize_info = { 0 };

/**
 * @brief Renderer options
*/
static struct
{
	int vsync;		/* Vertical sync; 0 = off, 1 = on */
	int frametime;		/* Frametime in microseconds, or -1 to sync with blitter */
	char shaderfile[512];	/* Shader file path. Match the length of openfilestring in win_dialog.c */
	int shaderfile_changed; /* Has shader file path changed. To prevent unnecessary shader recompilation. */
	int filter;		/* 0 = Nearest, 1 = Linear */
	int filter_changed;	/* Has filter changed. */
	mutex_t* mutex;
} options = { 0 };

/**
 * @brief Identifiers to OpenGL objects and uniforms.
 */
typedef struct
{
	GLuint vertexArrayID;
	GLuint vertexBufferID;
	GLuint textureID;
	GLuint unpackBufferID;
	GLuint shader_progID;

	/* Uniforms */

	GLint input_size;
	GLint output_size;
	GLint texture_size;
	GLint frame_count;
} gl_identifiers;

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
 * @brief Windows message handler for our window.
 * @param message
 * @param wParam
 * @param lParam 
 * @param fullscreen
*/
static void handle_window_messages(UINT message, WPARAM wParam, LPARAM lParam, int fullscreen)
{
	switch (message)
	{
	case WM_LBUTTONUP:
	case WM_LBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_RBUTTONDOWN:
		if (!fullscreen)
		{
			/* Bring main window to front. */
			SetForegroundWindow(GetAncestor(parent, GA_ROOT));

			/* Mouse events that enter and exit capture. */
			PostMessage(parent, message, wParam, lParam);
		}
		break;
	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
		if (fullscreen)
		{
			PostMessage(parent, message, wParam, lParam);
		}
		break;
	case WM_INPUT:
		if (fullscreen)
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
 * @brief (Re-)apply shaders to OpenGL context.
 * @param gl Identifiers from initialize
*/
static void apply_shaders(gl_identifiers* gl)
{
	GLuint old_shader_ID = 0;

	if (gl->shader_progID != 0)
		old_shader_ID = gl->shader_progID;

	if (strlen(options.shaderfile) > 0)
		gl->shader_progID = load_custom_shaders(options.shaderfile);
	else
		gl->shader_progID = 0;

	if (gl->shader_progID == 0)
		gl->shader_progID = load_default_shaders();

	glUseProgram(gl->shader_progID);

	/* Delete old shader if one exists (changing shader) */
	if (old_shader_ID != 0)
		glDeleteProgram(old_shader_ID);

	GLint vertex_coord = glGetAttribLocation(gl->shader_progID, "VertexCoord");
	if (vertex_coord != -1)
	{
		glEnableVertexAttribArray(vertex_coord);
		glVertexAttribPointer(vertex_coord, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), 0);
	}

	GLint tex_coord = glGetAttribLocation(gl->shader_progID, "TexCoord");
	if (tex_coord != -1)
	{
		glEnableVertexAttribArray(tex_coord);
		glVertexAttribPointer(tex_coord, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));
	}

	GLint color = glGetAttribLocation(gl->shader_progID, "Color");
	if (color != -1)
	{
		glEnableVertexAttribArray(color);
		glVertexAttribPointer(color, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void*)(4 * sizeof(GLfloat)));
	}

	GLint mvp_matrix = glGetUniformLocation(gl->shader_progID, "MVPMatrix");
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

	GLint frame_direction = glGetUniformLocation(gl->shader_progID, "FrameDirection");
	if (frame_direction != -1)
		glUniform1i(frame_direction, 1); /* always forward */

	gl->input_size = glGetUniformLocation(gl->shader_progID, "InputSize");
	gl->output_size = glGetUniformLocation(gl->shader_progID, "OutputSize");
	gl->texture_size = glGetUniformLocation(gl->shader_progID, "TextureSize");
	gl->frame_count = glGetUniformLocation(gl->shader_progID, "FrameCount");
}

/**
 * @brief Initialize OpenGL context
 * @return Identifiers
*/
static int initialize_glcontext(gl_identifiers* gl)
{
	/* Vertex, texture 2d coordinates and color (white) making a quad as triangle strip */
	static const GLfloat surface[] = {
		-1.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f,
		1.f, 1.f, 1.f, 0.f, 1.f, 1.f, 1.f, 1.f,
		-1.f, -1.f, 0.f, 1.f, 1.f, 1.f, 1.f, 1.f,
		1.f, -1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f
	};

	glGenVertexArrays(1, &gl->vertexArrayID);

	glBindVertexArray(gl->vertexArrayID);

	glGenBuffers(1, &gl->vertexBufferID);
	glBindBuffer(GL_ARRAY_BUFFER, gl->vertexBufferID);
	glBufferData(GL_ARRAY_BUFFER, sizeof(surface), surface, GL_STATIC_DRAW);

	glGenTextures(1, &gl->textureID);
	glBindTexture(GL_TEXTURE_2D, gl->textureID);

	static const GLfloat border_color[] = { 0.f, 0.f, 0.f, 1.f };
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border_color);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, options.filter ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, options.filter ? GL_LINEAR : GL_NEAREST);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, INIT_WIDTH, INIT_HEIGHT, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);

	glGenBuffers(1, &gl->unpackBufferID);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, gl->unpackBufferID);

	void* buf_ptr = NULL;

	if (GLAD_GL_ARB_buffer_storage)
	{
		/* Create persistent buffer for pixel transfer. */
		glBufferStorage(GL_PIXEL_UNPACK_BUFFER, BUFFERBYTES * BUFFERCOUNT, NULL, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);

		buf_ptr = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, BUFFERBYTES * BUFFERCOUNT, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);		
	}
	else
	{
		/* Fallback; create our own buffer. */
		buf_ptr = malloc(BUFFERBYTES * BUFFERCOUNT);

		glBufferData(GL_PIXEL_UNPACK_BUFFER, BUFFERBYTES * BUFFERCOUNT, NULL, GL_STREAM_DRAW);
	}

	if (buf_ptr == NULL)
		return 0; /* Most likely out of memory. */

	/* Split the buffer area for each blit_info and set them available for use. */
	for (int i = 0; i < BUFFERCOUNT; i++)
	{
		blit_info[i].buffer = (byte*)buf_ptr + BUFFERBYTES * i;
		atomic_flag_clear(&blit_info[i].in_use);
	}

	glClearColor(0.f, 0.f, 0.f, 1.f);

	apply_shaders(gl);

	return 1;
}

/**
 * @brief Clean up OpenGL context 
 * @param gl Identifiers from initialize 
*/
static void finalize_glcontext(gl_identifiers* gl)
{
	if (GLAD_GL_ARB_buffer_storage)
		glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
	else
		free(blit_info[0].buffer);

	glDeleteProgram(gl->shader_progID);
	glDeleteBuffers(1, &gl->unpackBufferID);
	glDeleteTextures(1, &gl->textureID);
	glDeleteBuffers(1, &gl->vertexBufferID);
	glDeleteVertexArrays(1, &gl->vertexArrayID);
}

/**
 * @brief Renders a frame and swaps the buffer
 * @param gl Identifiers from initialize
*/
static void render_and_swap(gl_identifiers* gl)
{
	static int frame_counter = 0;

	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	SDL_GL_SwapWindow(window);

	if (gl->frame_count != -1)
		glUniform1i(gl->frame_count, frame_counter = (frame_counter + 1) & 1023);
}

/**
 * @brief Handle failure in OpenGL thread.
 * Keeps the thread sleeping until closing.
*/
static void opengl_fail()
{
	if (window != NULL)
	{
		SDL_DestroyWindow(window);
		window = NULL;
	}

	/* TODO: Notify user. */

	WaitForSingleObject(sync_objects.closing, INFINITE);

	_endthread();
}

static void __stdcall opengl_debugmsg_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
	pclog("OpenGL: %s\n", message);
}

/**
 * @brief Main OpenGL thread proc.
 * 
 * OpenGL context should be accessed only from this single thread.
 * Events are used to synchronize communication.
*/
static void opengl_main(void* param)
{
	/* Initialize COM library for this thread before SDL does so. */
	CoInitializeEx(NULL, COINIT_MULTITHREADED);

	SDL_InitSubSystem(SDL_INIT_VIDEO);

	SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1"); /* Is this actually doing anything...? */

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	
	if (GLAD_GL_ARB_debug_output)
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG | SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	else
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);

	window = SDL_CreateWindow("86Box OpenGL Renderer", 0, 0, resize_info.width, resize_info.height, SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS);

	if (window == NULL)
	{
		pclog("OpenGL: failed to create OpenGL window.\n");
		opengl_fail();
	}

	/* Keep track of certain parameters, only changed in this thread to avoid race conditions */
	int fullscreen = resize_info.fullscreen, video_width = INIT_WIDTH, video_height = INIT_HEIGHT,
		output_width = resize_info.width, output_height = resize_info.height, frametime = options.frametime;

	SDL_SysWMinfo wmi = { 0 };
	SDL_VERSION(&wmi.version);
	SDL_GetWindowWMInfo(window, &wmi);

	if (wmi.subsystem != SDL_SYSWM_WINDOWS)
	{
		pclog("OpenGL: subsystem is not SDL_SYSWM_WINDOWS.\n");
		opengl_fail();
	}
	
	window_hwnd = wmi.info.win.window;

	if (!fullscreen)
		set_parent_binding(1);
	else
		SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);

	SDL_GLContext context = SDL_GL_CreateContext(window);

	if (context == NULL)
	{
		pclog("OpenGL: failed to create OpenGL context.\n");
		opengl_fail();
	}

	SDL_GL_SetSwapInterval(options.vsync);

	if (!gladLoadGLLoader(SDL_GL_GetProcAddress))
	{
		pclog("OpenGL: failed to set OpenGL loader.\n");
		SDL_GL_DeleteContext(context);
		opengl_fail();
	}
	
	if (GLAD_GL_ARB_debug_output)
	{
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
		glDebugMessageControlARB(GL_DONT_CARE, GL_DEBUG_TYPE_PERFORMANCE_ARB, GL_DONT_CARE, 0, 0, GL_FALSE);
		glDebugMessageCallbackARB(opengl_debugmsg_callback, NULL);
	}

	pclog("OpenGL vendor: %s\n", glGetString(GL_VENDOR));
	pclog("OpenGL renderer: %s\n", glGetString(GL_RENDERER));
	pclog("OpenGL version: %s\n", glGetString(GL_VERSION));
	pclog("OpenGL shader language version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

	gl_identifiers gl = { 0 };
	
	if (!initialize_glcontext(&gl))
	{
		pclog("OpenGL: failed to initialize.\n");
		finalize_glcontext(&gl);
		SDL_GL_DeleteContext(context);
		opengl_fail();
	}

	if (gl.frame_count != -1)
		glUniform1i(gl.frame_count, 0);
	if (gl.output_size != -1)
		glUniform2f(gl.output_size, output_width, output_height);

	uint32_t last_swap = plat_get_micro_ticks() - frametime;

	int read_pos = 0; /* Buffer index of next read operation. */

	/* Render loop */
	int closing = 0;
	while (!closing)
	{
		/* Rendering is done right after handling an event. */
		if (frametime < 0)
			render_and_swap(&gl);

		DWORD wait_result = WAIT_TIMEOUT;

		do
		{
			/* Rendering is timed by frame capping. */
			if (frametime >= 0)
			{
				uint32_t ticks = plat_get_micro_ticks();

				uint32_t elapsed = ticks - last_swap;
				
				if (elapsed + 1000 > frametime)
				{
					/* Spin the remaining time (< 1ms) to next frame */
					while (elapsed < frametime)
					{
						Sleep(0); /* Yield processor time */
						ticks = plat_get_micro_ticks();
						elapsed = ticks - last_swap;
					}

					render_and_swap(&gl);
					last_swap = ticks;
				}
			}

			/* Check if commands that use buffers have been completed. */
			for (int i = 0; i < BUFFERCOUNT; i++)
			{
				if (blit_info[i].sync != NULL && glClientWaitSync(blit_info[i].sync, GL_SYNC_FLUSH_COMMANDS_BIT, 0) != GL_TIMEOUT_EXPIRED)
				{
					glDeleteSync(blit_info[i].sync);
					blit_info[i].sync = NULL;
					atomic_flag_clear(&blit_info[i].in_use);
				}
			}

			/* Handle window messages */
			MSG msg;
			if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			{
				if (msg.hwnd == window_hwnd)
					handle_window_messages(msg.message, msg.wParam, msg.lParam, fullscreen);
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

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
			blit_info_t* info = &blit_info[read_pos];

			if (video_width != info->w || video_height != info->h)
			{
				video_width = info->w;
				video_height = info->h;

				/* Resize the texture */
				glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, video_width, video_height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
				glBindBuffer(GL_PIXEL_UNPACK_BUFFER, gl.unpackBufferID);

				if (fullscreen)
					SetEvent(sync_objects.resize);
			}

			/* Clip to height. y2 can be out-of-bounds. */
			int sub_height = MIN(info->y2, info->h) - info->y1;

			if (!GLAD_GL_ARB_buffer_storage)
			{
				/* Fallback method, copy data to pixel buffer. */
				glBufferSubData(GL_PIXEL_UNPACK_BUFFER, BUFFERBYTES * read_pos, info->w * sub_height * sizeof(uint32_t), info->buffer);
			}

			/* Update texture from pixel buffer. */
			glPixelStorei(GL_UNPACK_SKIP_PIXELS, BUFFERPIXELS * read_pos);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, info->y1, info->w, sub_height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);

			/* Add fence to track when above gl commands are complete. */
			info->sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

			read_pos = (read_pos + 1) % BUFFERCOUNT;

			/* Update uniforms */
			if (gl.input_size != -1)
				glUniform2f(gl.input_size, video_width, video_height);
			if (gl.texture_size != -1)
				glUniform2f(gl.texture_size, video_width, video_height);
		}
		else if (sync_event == sync_objects.resize)
		{
			thread_wait_mutex(resize_info.mutex);

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

				output_width = width - pad_x;
				output_height = height - pad_y;

				glViewport(pad_x / 2, pad_y / 2, output_width, output_height);

				if (gl.output_size != -1)
					glUniform2f(gl.output_size, output_width, output_height);
			}
			else
			{
				SDL_SetWindowSize(window, resize_info.width, resize_info.height);

				/* SWP_NOZORDER is needed for child window and SDL doesn't enable it. */
				SetWindowPos(window_hwnd, parent, 0, 0, resize_info.width, resize_info.height, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOACTIVATE);

				output_width = resize_info.width;
				output_height = resize_info.height;

				glViewport(0, 0, resize_info.width, resize_info.height);

				if (gl.output_size != -1)
					glUniform2f(gl.output_size, resize_info.width, resize_info.height);
			}

			thread_release_mutex(resize_info.mutex);
		}
		else if (sync_event == sync_objects.reload)
		{
			thread_wait_mutex(options.mutex);

			frametime = options.frametime;

			SDL_GL_SetSwapInterval(options.vsync);

			if (options.shaderfile_changed)
			{
				/* Change shader program. */
				apply_shaders(&gl);

				/* Uniforms need to be updated after proram change. */
				if (gl.input_size != -1)
					glUniform2f(gl.input_size, video_width, video_height);
				if (gl.output_size != -1)
					glUniform2f(gl.output_size, output_width, output_height);
				if (gl.texture_size != -1)
					glUniform2f(gl.texture_size, video_width, video_height);
				if (gl.frame_count != -1)
					glUniform1i(gl.frame_count, 0);

				options.shaderfile_changed = 0;
			}

			if (options.filter_changed)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, options.filter ? GL_LINEAR : GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, options.filter ? GL_LINEAR : GL_NEAREST);

				options.filter_changed = 0;
			}

			thread_release_mutex(options.mutex);
		}

		/* Keep cursor hidden in full screen and mouse capture */
		int show_cursor = !(fullscreen || !!mouse_capture);
		if (SDL_ShowCursor(-1) != show_cursor)
				SDL_ShowCursor(show_cursor);
	}

	for (int i = 0; i < BUFFERCOUNT; i++)
	{
		if (blit_info[i].sync != NULL)
			glDeleteSync(blit_info[i].sync);
	}

	finalize_glcontext(&gl);

	SDL_GL_DeleteContext(context);

	set_parent_binding(0);

	SDL_DestroyWindow(window);

	window = NULL;

	CoUninitialize();
}

static void opengl_blit(int x, int y, int w, int h)
{
	int yy;

	if ((h <= 0) || (buffer32 == NULL) || (thread == NULL) ||
		atomic_flag_test_and_set(&blit_info[write_pos].in_use))
	{
		video_blit_complete();
		return;
	}

	for (yy = 0; yy < h; yy++) {
		if ((y + yy) >= 0 && (y + yy) < buffer32->h)
			memcpy(blit_info[write_pos].buffer + (yy * w * sizeof(uint32_t)),
			       &(((uint32_t *) buffer32->line[y + yy])[x]), w * sizeof(uint32_t));
	}

	video_blit_complete();

	blit_info[write_pos].y1 = 0;
	blit_info[write_pos].y2 = h - 1;
	blit_info[write_pos].w = w;
	blit_info[write_pos].h = h;

	write_pos = (write_pos + 1) % BUFFERCOUNT;

	ReleaseSemaphore(sync_objects.blit_waiting, 1, NULL);
}

static int framerate_to_frametime(int framerate)
{
	if (framerate < 0)
		return -1;

	return (int)ceilf(1.e6f / (float)framerate);
}

int opengl_init(HWND hwnd)
{
	if (thread != NULL)
		return 0;

	for (int i = 0; i < sizeof(sync_objects) / sizeof(HANDLE); i++)
		sync_objects.asArray[i] = CreateEvent(NULL, FALSE, FALSE, NULL);

	sync_objects.closing = CreateEvent(NULL, FALSE, FALSE, NULL);
	sync_objects.resize = CreateEvent(NULL, FALSE, FALSE, NULL);
	sync_objects.reload = CreateEvent(NULL, FALSE, FALSE, NULL);
	sync_objects.blit_waiting = CreateSemaphore(NULL, 0, BUFFERCOUNT * 2, NULL);

	parent = hwnd;
	
	RECT parent_size;

	GetWindowRect(parent, &parent_size);

	resize_info.width = parent_size.right - parent_size.left;
	resize_info.height = parent_size.bottom - parent_size.top;
	resize_info.fullscreen = video_fullscreen & 1;
	resize_info.scaling_mode = video_fullscreen_scale;
	resize_info.mutex = thread_create_mutex();

	options.vsync = video_vsync;
	options.frametime = framerate_to_frametime(video_framerate);
	strcpy_s(options.shaderfile, sizeof(options.shaderfile), video_shader);
	options.shaderfile_changed = 0;
	options.filter = video_filter_method;
	options.filter_changed = 0;
	options.mutex = thread_create_mutex();

	blit_info = (blit_info_t*)malloc(BUFFERCOUNT * sizeof(blit_info_t));
	memset(blit_info, 0, BUFFERCOUNT * sizeof(blit_info_t));

	/* Buffers are not yet allocated, set them as in use. */
	for (int i = 0; i < BUFFERCOUNT; i++)
		atomic_flag_test_and_set(&blit_info[i].in_use);

	write_pos = 0;

	thread = thread_create(opengl_main, (void*)NULL);

	atexit(opengl_close);

	video_setblit(opengl_blit);

	return 1;
}

int opengl_pause(void)
{
	return 0;
}

void opengl_close(void)
{
	if (thread == NULL)
		return;

	SetEvent(sync_objects.closing);

	thread_wait(thread, -1);

	thread_close_mutex(resize_info.mutex);
	thread_close_mutex(options.mutex);

	thread = NULL;

	free(blit_info);

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

	thread_wait_mutex(resize_info.mutex);
	
	resize_info.fullscreen = fs;
	resize_info.scaling_mode = video_fullscreen_scale;

	thread_release_mutex(resize_info.mutex);

	SetEvent(sync_objects.resize);
}

void opengl_resize(int w, int h)
{
	if (thread == NULL)
		return;

	thread_wait_mutex(resize_info.mutex);

	resize_info.width = w;
	resize_info.height = h;
	resize_info.scaling_mode = video_fullscreen_scale;

	thread_release_mutex(resize_info.mutex);

	SetEvent(sync_objects.resize);
}

void opengl_reload(void)
{
	if (thread == NULL)
		return;

	thread_wait_mutex(options.mutex);

	options.vsync = video_vsync;
	options.frametime = framerate_to_frametime(video_framerate);
	
	if (strcmp(video_shader, options.shaderfile) != 0)
	{
		strcpy_s(options.shaderfile, sizeof(options.shaderfile), video_shader);
		options.shaderfile_changed = 1;
	}

	if (video_filter_method != options.filter)
	{
		options.filter = video_filter_method;
		options.filter_changed = 1;
	}

	thread_release_mutex(options.mutex);

	SetEvent(sync_objects.reload);
}