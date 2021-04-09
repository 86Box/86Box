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

#include <stdatomic.h>

#include <86box/plat.h>
#include <86box/video.h>
#include <86box/win_opengl.h>

static thread_t* thread = NULL;

static SDL_Window* window = NULL;
static SDL_SysWMinfo wmi = {};

static HWND parent = NULL;
static RECT last_rect = {};

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

static HANDLE blit_done = NULL;

static volatile struct
{
	int x, y, y1, y2, w, h, resized;
} blit_info = {};

typedef struct
{
	GLuint vertexArrayID;
	GLuint vertexBufferID;
	GLuint textureID;
	GLuint shader_progID;
} gl_objects;

static const GLchar* v_shader = "#version 330 core\n\
layout(location = 0) in vec2 pos;\n\
layout(location = 1) in vec2 tex_in;\n\
out vec2 tex;\n\
void main(){\n\
	gl_Position = vec4(pos, 0.0, 1.0);\n\
	tex = tex_in;\n\
}\n";

static const GLchar* f_shader = "#version 330 core\n\
in vec2 tex;\n\
out vec4 color;\n\
uniform sampler2D texs;\n\
void main() {\n\
	color = texture(texs, tex);\n\
	if (int(gl_FragCoord.y) % 2 == 0)\n\
		color = color * vec4(0.5,0.5,0.5,1.0);\n\
}\n";

static GLuint LoadShaders()
{
	GLuint vs_id = glCreateShader(GL_VERTEX_SHADER);
	GLuint fs_id = glCreateShader(GL_FRAGMENT_SHADER);

	GLint compile_status = GL_FALSE;
	int info_log_length;
	char info_log_text[200];

	glShaderSource(vs_id, 1, &v_shader, NULL);
	glCompileShader(vs_id);

	glGetShaderiv(vs_id, GL_COMPILE_STATUS, &compile_status);
	glGetShaderiv(vs_id, GL_INFO_LOG_LENGTH, &info_log_length);

	glGetShaderInfoLog(vs_id, info_log_length, NULL, info_log_text);

	glShaderSource(fs_id, 1, &f_shader, NULL);
	glCompileShader(fs_id);

	glGetShaderiv(fs_id, GL_COMPILE_STATUS, &compile_status);
	glGetShaderiv(fs_id, GL_INFO_LOG_LENGTH, &info_log_length);

	glGetShaderInfoLog(fs_id, info_log_length, NULL, info_log_text);

	GLuint prog_id = glCreateProgram();

	glAttachShader(prog_id, vs_id);
	glAttachShader(prog_id, fs_id);

	glLinkProgram(prog_id);

	glDetachShader(prog_id, vs_id);
	glDetachShader(prog_id, fs_id);

	glDeleteShader(vs_id);
	glDeleteShader(fs_id);

	return prog_id;
}

static void SetParentBinding(int enable)
{
	if (wmi.subsystem != SDL_SYSWM_WINDOWS)
		return;

	long style = GetWindowLong(wmi.info.win.window, GWL_STYLE);
	
	if (enable)
		style |= WS_CHILD;
	else
		style &= ~WS_CHILD;

	SetWindowLong(wmi.info.win.window, GWL_STYLE, style);

	SetParent(wmi.info.win.window, enable ? parent : NULL);
}

static gl_objects initialize_glcontext()
{
	static const GLfloat surface[] = {
		-1.f, 1.f, 0.f, 0.f,
		1.f, 1.f, 1.f, 0.f,
		-1.f, -1.f, 0.f, 1.f,
		1.f, -1.f, 1.f, 1.f
	};

	gl_objects obj = {};

	glGenVertexArrays(1, &obj.vertexArrayID);

	glBindVertexArray(obj.vertexArrayID);

	glGenBuffers(1, &obj.vertexBufferID);
	glBindBuffer(GL_ARRAY_BUFFER, obj.vertexBufferID);
	glBufferData(GL_ARRAY_BUFFER, sizeof(surface), surface, GL_STATIC_DRAW);

	glGenTextures(1, &obj.textureID);
	glBindTexture(GL_TEXTURE_2D, obj.textureID);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 640, 400, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);

	glClearColor(0.f, 0.f, 0.f, 1.f);

	obj.shader_progID = LoadShaders();

	glUseProgram(obj.shader_progID);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), 0);

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));

	return obj;
}

static void finalize_glcontext(gl_objects obj)
{
	glDeleteProgram(obj.shader_progID);
	glDeleteTextures(1, &obj.textureID);
	glDeleteBuffers(1, &obj.vertexBufferID);
	glDeleteVertexArrays(1, &obj.vertexArrayID);
}

static void opengl_main()
{
	RECT rect;
	GetWindowRect(parent, &rect);
	CopyRect(&last_rect, &rect);

	SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

	window = SDL_CreateWindow("86Box OpenGL Renderer", 0, 0, rect.right - rect.left, rect.bottom - rect.top, SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS);
	
	SDL_VERSION(&wmi.version);
	SDL_GetWindowWMInfo(window, &wmi);

	SetParentBinding(1);

	SDL_GLContext context = SDL_GL_CreateContext(window);

	gladLoadGLLoader(SDL_GL_GetProcAddress);

	gl_objects obj = initialize_glcontext();

	/* Render loop */
	int closing = 0;
	while (!closing)
	{
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		SDL_GL_SwapWindow(window);

		DWORD wait_result = WAIT_TIMEOUT;

		do
		{
			/* SDL_Window event handlers */
			SDL_Event event;

			while (SDL_PollEvent(&event))
			{
				if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
					SetForegroundWindow(GetParent(parent));
				else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT)
					plat_mouse_capture(1);
			}

			/* Wait for sync events */
			wait_result = WaitForMultipleObjects(sizeof(sync_objects) / sizeof(HANDLE), sync_objects.asArray, FALSE, 1);

		} while (wait_result == WAIT_TIMEOUT);

		HANDLE sync_event = sync_objects.asArray[wait_result - WAIT_OBJECT_0];

		if (sync_event == sync_objects.closing)
		{
			closing = 1;
		}
		else if (sync_event == sync_objects.blit_waiting)
		{
			if (blit_info.resized)
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, blit_info.w, blit_info.h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);

			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, blit_info.y1, blit_info.w, blit_info.y2 - blit_info.y1, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, &(render_buffer->dat)[blit_info.y1 * blit_info.w]);

			SetEvent(blit_done);
		}
		else if (sync_event == sync_objects.resize)
		{
			SetParentBinding(0);
			
			GetWindowRect(parent, &rect);
			
			SDL_SetWindowSize(window, rect.right - rect.left, rect.bottom - rect.top);			
			
			glViewport(0, 0, rect.right - rect.left, rect.bottom - rect.top);
			
			SetParentBinding(1);

			//SetForegroundWindow(GetParent(parent));
		}
	}

	finalize_glcontext(obj);

	SDL_GL_DeleteContext(context);

	SetParentBinding(0);

	SDL_DestroyWindow(window);

	window = NULL;
}

static void
opengl_blit(int x, int y, int y1, int y2, int w, int h)
{
	if (y1 == y2 || h <= 0 || render_buffer == NULL)
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
}

void opengl_set_fs(int fs)
{
	if (window != NULL)
	{
		SetParentBinding(fs ? 0 : 1);

		SDL_SetWindowFullscreen(window, fs ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
	}
}

void opengl_enable(int enable)
{
	if (enable == 0 || parent == NULL)
		return;

	RECT rect;
	GetWindowRect(parent, &rect);
	
	if (!EqualRect(&rect, &last_rect))
	{
		CopyRect(&last_rect, &rect);

		SetEvent(sync_objects.resize);
	}
}