// minitrace
// Copyright 2014 by Henrik Rydgård
// http://www.github.com/hrydgard/minitrace
// Released under the MIT license.

// See minitrace.h for basic documentation.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#pragma warning (disable:4996)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifndef __MINGW32__
#define __thread __declspec(thread)
#endif
#define pthread_cond_t CONDITION_VARIABLE
#define pthread_cond_init(a) InitializeConditionVariable(a)
#define pthread_cond_wait(a, b) SleepConditionVariableCS(a, b, INFINITE)
#define pthread_cond_signal(a) WakeConditionVariable(a)
#define pthread_mutex_t CRITICAL_SECTION
#define pthread_mutex_init(a, b) InitializeCriticalSection(a)
#define pthread_mutex_lock(a) EnterCriticalSection(a)
#define pthread_mutex_unlock(a) LeaveCriticalSection(a)
#define pthread_mutex_destroy(a) DeleteCriticalSection(a)
#else
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <stdatomic.h>
#include <minitrace/minitrace.h>

#ifdef __GNUC__
#define ATTR_NORETURN __attribute__((noreturn))
#else
#define ATTR_NORETURN
#endif

#define ARRAY_SIZE(x) sizeof(x)/sizeof(x[0])
#define TRUE 1
#define FALSE 0

// Ugh, this struct is already pretty heavy.
// Will probably need to move arguments to a second buffer to support more than one.
typedef struct raw_event {
	const char *name;
	const char *cat;
	void *id;
	int64_t ts;
	uint32_t pid;
	uint32_t tid;
	char ph;
	mtr_arg_type arg_type;
	const char *arg_name;
	union {
		const char *a_str;
		int a_int;
		double a_double;
	};
} raw_event_t;

static raw_event_t *event_buffer;
static raw_event_t *flush_buffer;
static volatile int event_count;
static __attribute__ ((aligned (32))) atomic_long is_tracing = FALSE;
static __attribute__ ((aligned (32))) atomic_long stop_flushing_requested = FALSE;
static int is_flushing = FALSE;
static int events_in_progress = 0;
static int64_t time_offset;
static int first_line = 1;
static FILE *f;
static __thread int cur_thread_id;	// Thread local storage
static int cur_process_id;
static pthread_mutex_t mutex;
static pthread_mutex_t event_mutex;
static pthread_cond_t buffer_not_full_cond;
static pthread_cond_t buffer_full_cond;

#define STRING_POOL_SIZE 100
static char *str_pool[100];

// forward declaration
void mtr_flush_with_state(int);

// Tiny portability layer.
// Exposes:
//	 get_cur_thread_id()
//	 get_cur_process_id()
//	 mtr_time_s()
//	 pthread basics
#ifdef _WIN32

static int get_cur_thread_id() {
	return (int)GetCurrentThreadId();
}
static int get_cur_process_id() {
	return (int)GetCurrentProcessId();
}

static uint64_t _frequency = 0;
static uint64_t _starttime = 0;
double mtr_time_s() {
	if (_frequency == 0) {
		QueryPerformanceFrequency((LARGE_INTEGER*)&_frequency);
		QueryPerformanceCounter((LARGE_INTEGER*)&_starttime);
	}
	__int64 time;
	QueryPerformanceCounter((LARGE_INTEGER*)&time);
	return ((double) (time - _starttime) / (double) _frequency);
}

// Ctrl+C handling for Windows console apps
static BOOL WINAPI CtrlHandler(DWORD fdwCtrlType) {
        if (atomic_load(&is_tracing) && fdwCtrlType == CTRL_C_EVENT) {
		printf("Ctrl-C detected! Flushing trace and shutting down.\n\n");
		mtr_flush();
		mtr_shutdown();
	}
	ExitProcess(1);
}

void mtr_register_sigint_handler() {
	// For console apps:
	SetConsoleCtrlHandler(&CtrlHandler, TRUE);
}

HANDLE thread_handle;

static DWORD WINAPI thread_flush_proc(void* param) {
    while(TRUE) {
        mtr_flush_with_state(FALSE);
        if(atomic_load(&stop_flushing_requested)) {
            break;
        }
    }
    return 0;
}

static void init_flushing_thread(void) {
    pthread_mutex_lock(&mutex);
    is_flushing = FALSE;
    pthread_mutex_unlock(&mutex);
    thread_handle = CreateThread(NULL, 0, thread_flush_proc, (void*)0, 0, NULL);
}

static void join_flushing_thread(void) {
    WaitForSingleObject(thread_handle, INFINITE);
}

#else

static inline int get_cur_thread_id() {
	return (int)(intptr_t)pthread_self();
}
static inline int get_cur_process_id() {
	return (int)getpid();
}

static pthread_t thread_handle = 0;
static void* thread_flush_proc(void* param) {
    while(1) {
        mtr_flush_with_state(0);
        if(atomic_load(&stop_flushing_requested)) {
            break;
        }
    }
    return 0;
}
static void init_flushing_thread(void) {
    pthread_mutex_lock(&mutex);
    is_flushing = FALSE;
    pthread_mutex_unlock(&mutex);
    if (pthread_create(&thread_handle, NULL, thread_flush_proc, NULL) != 0)
    {
        thread_handle = 0;
    }
}

static void join_flushing_thread(void) {
    if (thread_handle) pthread_join(thread_handle, NULL);
    thread_handle = 0;
}

#if defined(BLACKBERRY)
double mtr_time_s() {
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time); // Linux must use CLOCK_MONOTONIC_RAW due to time warps
	return time.tv_sec + time.tv_nsec / 1.0e9;
}
#else
double mtr_time_s() {
	static time_t start;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	if (start == 0) {
		start = tv.tv_sec;
	}
	tv.tv_sec -= start;
	return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}
#endif	// !BLACKBERRY

static void termination_handler(int signum) ATTR_NORETURN;
static void termination_handler(int signum) {
	(void) signum;
	if (is_tracing) {
		printf("Ctrl-C detected! Flushing trace and shutting down.\n\n");
		mtr_flush();
		fwrite("\n]}\n", 1, 4, f);
		fclose(f);
	}
	exit(1);
}

void mtr_register_sigint_handler() {
#ifndef MTR_ENABLED
	return;
#endif
	// Avoid altering set-to-be-ignored handlers while registering.
	if (signal(SIGINT, &termination_handler) == SIG_IGN)
		signal(SIGINT, SIG_IGN);
}

#endif

void mtr_init_from_stream(void *stream) {
#ifndef MTR_ENABLED
	return;
#endif
	event_buffer = (raw_event_t *)malloc(INTERNAL_MINITRACE_BUFFER_SIZE * sizeof(raw_event_t));
	flush_buffer = (raw_event_t *)malloc(INTERNAL_MINITRACE_BUFFER_SIZE * sizeof(raw_event_t));
	event_count = 0;
	f = (FILE *)stream;
	const char *header = "{\"traceEvents\":[\n";
	fwrite(header, 1, strlen(header), f);
	time_offset = (uint64_t)(mtr_time_s() * 1000000);
	first_line = 1;
	pthread_mutex_init(&mutex, 0);
	pthread_mutex_init(&event_mutex, 0);
}

void mtr_init(const char *json_file) {
#ifndef MTR_ENABLED
	return;
#endif
	mtr_init_from_stream(fopen(json_file, "wb"));
}

void mtr_shutdown() {
	int i;
#ifndef MTR_ENABLED
	return;
#endif

	mtr_flush_with_state(TRUE);

	fwrite("\n]}\n", 1, 4, f);
	fclose(f);
	pthread_mutex_destroy(&mutex);
	pthread_mutex_destroy(&event_mutex);
	f = 0;
	free(event_buffer);
	event_buffer = 0;
	for (i = 0; i < STRING_POOL_SIZE; i++) {
		if (str_pool[i]) {
			free(str_pool[i]);
			str_pool[i] = 0;
		}
	}
}

const char *mtr_pool_string(const char *str) {
	int i;
	for (i = 0; i < STRING_POOL_SIZE; i++) {
		if (!str_pool[i]) {
			str_pool[i] = (char*)malloc(strlen(str) + 1);
			strcpy(str_pool[i], str);
			return str_pool[i];
		} else {
			if (!strcmp(str, str_pool[i]))
				return str_pool[i];
		}
	}
	return "string pool full";
}

void mtr_start() {
#ifndef MTR_ENABLED
	return;
#endif
#ifdef _WIN32
    pthread_cond_init(&buffer_not_full_cond);
    pthread_cond_init(&buffer_full_cond);
#else
    pthread_cond_init(&buffer_not_full_cond, NULL);
    pthread_cond_init(&buffer_full_cond, NULL);
#endif
    atomic_store(&is_tracing, TRUE);
	init_flushing_thread();
}

void mtr_stop() {
#ifndef MTR_ENABLED
	return;
#endif
	atomic_store(&is_tracing, FALSE);
	atomic_store(&stop_flushing_requested, TRUE);
	pthread_cond_signal(&buffer_not_full_cond);
	pthread_cond_signal(&buffer_full_cond);
	join_flushing_thread();
	atomic_store(&stop_flushing_requested, FALSE);
}

// TODO: fwrite more than one line at a time.
// Flushing is thread safe and process async
// using double-buffering mechanism.
// Aware: only one flushing process may be
// running at any point of time
void mtr_flush_with_state(int is_last) {
#ifndef MTR_ENABLED
	return;
#endif
	int i = 0;
	char linebuf[1024];
	char arg_buf[1024];
	char id_buf[256];
	int event_count_copy = 0;
	int events_in_progress_copy = 1;
	raw_event_t *event_buffer_tmp = NULL;

	// small critical section to swap buffers
	// - no any new events can be spawn while
	//   swapping since they tied to the same mutex
	// - checks for any flushing in process
	pthread_mutex_lock(&mutex);
	// if not flushing already
	if (is_flushing) {
		pthread_mutex_unlock(&mutex);
		return;
	}
	is_flushing = TRUE;
	if(!is_last) {
	    while(event_count < INTERNAL_MINITRACE_BUFFER_SIZE && atomic_load(&is_tracing)) {
		pthread_cond_wait(&buffer_full_cond, &mutex);
	    }
	}
	event_count_copy = event_count;
	event_buffer_tmp = flush_buffer;
	flush_buffer = event_buffer;
	event_buffer = event_buffer_tmp;
	event_count = 0;
	// waiting for any unfinished events before swap
	while (events_in_progress_copy != 0) {
		pthread_mutex_lock(&event_mutex);
		events_in_progress_copy = events_in_progress;
		pthread_mutex_unlock(&event_mutex);
	}
	pthread_mutex_unlock(&mutex);
	pthread_cond_signal(&buffer_not_full_cond);

	for (i = 0; i < event_count_copy; i++) {
		raw_event_t *raw = &flush_buffer[i];
		int len;
		switch (raw->arg_type) {
		case MTR_ARG_TYPE_INT:
			snprintf(arg_buf, ARRAY_SIZE(arg_buf), "\"%s\":%i", raw->arg_name, raw->a_int);
			break;
		case MTR_ARG_TYPE_STRING_CONST:
			snprintf(arg_buf, ARRAY_SIZE(arg_buf), "\"%s\":\"%s\"", raw->arg_name, raw->a_str);
			break;
		case MTR_ARG_TYPE_STRING_COPY:
			if (strlen(raw->a_str) > 700) {
				snprintf(arg_buf, ARRAY_SIZE(arg_buf), "\"%s\":\"%.*s\"", raw->arg_name, 700, raw->a_str);
			} else {
				snprintf(arg_buf, ARRAY_SIZE(arg_buf), "\"%s\":\"%s\"", raw->arg_name, raw->a_str);
			}
			break;
		case MTR_ARG_TYPE_NONE:
			arg_buf[0] = '\0';
			break;
		}
		if (raw->id) {
			switch (raw->ph) {
			case 'S':
			case 'T':
			case 'F':
				// TODO: Support full 64-bit pointers
				snprintf(id_buf, ARRAY_SIZE(id_buf), ",\"id\":\"0x%08x\"", (uint32_t)(uintptr_t)raw->id);
				break;
			case 'X':
				snprintf(id_buf, ARRAY_SIZE(id_buf), ",\"dur\":%i", (int)raw->a_double);
				break;
			}
		} else {
			id_buf[0] = 0;
		}
		const char *cat = raw->cat;
#ifdef _WIN32
		// On Windows, we often end up with backslashes in category.
		char temp[256];
		{
			int len = (int)strlen(cat);
			int i;
			if (len > 255) len = 255;
			for (i = 0; i < len; i++) {
				temp[i] = cat[i] == '\\' ? '/' : cat[i];
			}
			temp[len] = 0;
			cat = temp;
		}
#endif

		len = snprintf(linebuf, ARRAY_SIZE(linebuf), "%s{\"cat\":\"%s\",\"pid\":%i,\"tid\":%i,\"ts\":%" PRId64 ",\"ph\":\"%c\",\"name\":\"%s\",\"args\":{%s}%s}",
				first_line ? "" : ",\n",
				cat, raw->pid, raw->tid, raw->ts - time_offset, raw->ph, raw->name, arg_buf, id_buf);
		fwrite(linebuf, 1, len, f);
		first_line = 0;

		if (raw->arg_type == MTR_ARG_TYPE_STRING_COPY) {
			free((void*)raw->a_str);
		}
		#ifdef MTR_COPY_EVENT_CATEGORY_AND_NAME
		free(raw->name);
		free(raw->cat);
		#endif
	}

	pthread_mutex_lock(&mutex);
	is_flushing = is_last;
	pthread_mutex_unlock(&mutex);
}

void mtr_flush() {
	mtr_flush_with_state(FALSE);
}

void internal_mtr_raw_event(const char *category, const char *name, char ph, void *id) {
#ifndef MTR_ENABLED
	return;
#endif

	if (!atomic_load(&is_tracing)) {
		return;
	}
	pthread_mutex_lock(&mutex);
	while(event_count >= INTERNAL_MINITRACE_BUFFER_SIZE && atomic_load(&is_tracing)) {
	    pthread_cond_wait(&buffer_not_full_cond, &mutex);
	}
	raw_event_t *ev = &event_buffer[event_count];
	++event_count;
	pthread_mutex_lock(&event_mutex);
	++events_in_progress;
	pthread_mutex_unlock(&event_mutex);
	int local_event_count = event_count;
	pthread_mutex_unlock(&mutex);
	if(local_event_count >= INTERNAL_MINITRACE_BUFFER_SIZE) {
	    pthread_cond_signal(&buffer_full_cond);
	}

	double ts = mtr_time_s();

	if (!cur_thread_id) {
		cur_thread_id = get_cur_thread_id();
	}
	if (!cur_process_id) {
		cur_process_id = get_cur_process_id();
	}

#ifdef MTR_COPY_EVENT_CATEGORY_AND_NAME
	const size_t category_len = strlen(category);
	ev->cat = malloc(category_len + 1);
	strcpy(ev->cat, category);

	const size_t name_len = strlen(name);
	ev->name = malloc(name_len + 1);
	strcpy(ev->name, name);

#else
	ev->cat = category;
	ev->name = name;
#endif

	ev->id = id;
	ev->ph = ph;
	if (ev->ph == 'X') {
		double x;
		memcpy(&x, id, sizeof(double));
		ev->ts = (int64_t)(x * 1000000);
		ev->a_double = (ts - x) * 1000000;
	} else {
		ev->ts = (int64_t)(ts * 1000000);
	}
	ev->tid = cur_thread_id;
	ev->pid = cur_process_id;
	ev->arg_type = MTR_ARG_TYPE_NONE;

	pthread_mutex_lock(&event_mutex);
	--events_in_progress;
	pthread_mutex_unlock(&event_mutex);
}

void internal_mtr_raw_event_arg(const char *category, const char *name, char ph, void *id, mtr_arg_type arg_type, const char *arg_name, void *arg_value) {
#ifndef MTR_ENABLED
	return;
#endif
	if (!atomic_load(&is_tracing)) {
		return;
	}
	pthread_mutex_lock(&mutex);
	while(event_count >= INTERNAL_MINITRACE_BUFFER_SIZE && atomic_load(&is_tracing)) {
	    pthread_cond_wait(&buffer_not_full_cond, &mutex);
	}
	raw_event_t *ev = &event_buffer[event_count];
	++event_count;
	pthread_mutex_lock(&event_mutex);
	++events_in_progress;
	pthread_mutex_unlock(&event_mutex);
	int local_event_count = event_count;
	pthread_mutex_unlock(&mutex);
	if(local_event_count >= INTERNAL_MINITRACE_BUFFER_SIZE) {
	    pthread_cond_signal(&buffer_full_cond);
	}


	if (!cur_thread_id) {
		cur_thread_id = get_cur_thread_id();
	}
	if (!cur_process_id) {
		cur_process_id = get_cur_process_id();
	}
	double ts = mtr_time_s();

#ifdef MTR_COPY_EVENT_CATEGORY_AND_NAME
	const size_t category_len = strlen(category);
	ev->cat = malloc(category_len + 1);
	strcpy(ev->cat, category);

	const size_t name_len = strlen(name);
	ev->name = malloc(name_len + 1);
	strcpy(ev->name, name);

#else
	ev->cat = category;
	ev->name = name;
#endif

	ev->id = id;
	ev->ts = (int64_t)(ts * 1000000);
	ev->ph = ph;
	ev->tid = cur_thread_id;
	ev->pid = cur_process_id;
	ev->arg_type = arg_type;
	ev->arg_name = arg_name;
	switch (arg_type) {
	case MTR_ARG_TYPE_INT: ev->a_int = (int)(uintptr_t)arg_value; break;
	case MTR_ARG_TYPE_STRING_CONST:	ev->a_str = (const char*)arg_value; break;
	case MTR_ARG_TYPE_STRING_COPY: ev->a_str = strdup((const char*)arg_value); break;
	case MTR_ARG_TYPE_NONE: break;
	}

	pthread_mutex_lock(&event_mutex);
	--events_in_progress;
	pthread_mutex_unlock(&event_mutex);
}
