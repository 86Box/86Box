//
// Created by claunia on 13/8/25.
//

#ifndef LIBAARUFORMAT_LOG_H
#define LIBAARUFORMAT_LOG_H

#include <stdarg.h>
#include <stdio.h>

// Uncomment to enable tracing
// #define ENABLE_TRACE
// Uncomment to use slog instead of stderr
// #define USE_SLOG

#ifdef ENABLE_TRACE
#ifdef USE_SLOG
#include "slog.h"

#define TRACE(fmt, ...) slog_trace(fmt, ##__VA_ARGS__)
#else
#define TRACE(fmt, ...) fprintf(stderr, "[TRACE] %s:%d:%s(): " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#endif
#else
#define TRACE(fmt, ...)
#endif

#include <stdarg.h>
#include <stdio.h>

#ifdef ENABLE_FATAL
#ifdef USE_SLOG
#include "slog.h"

#define FATAL(fmt, ...) slog_fatal(fmt, ##__VA_ARGS__)
#else
#define FATAL(fmt, ...) fprintf(stderr, "[FATAL] %s:%d:%s(): " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#endif
#else
#define FATAL(fmt, ...)
#endif

#endif  // LIBAARUFORMAT_LOG_H
