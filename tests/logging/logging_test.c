/* Exercise the real legacy/device loggers and the caller-side release policy. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/log.h>
extern FILE *stdlog;

static unsigned checks, failures, hooks;
static char last_line[8192];
#define CHECK(expr) do { checks++; if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; } } while (0)
FILE *plat_fopen(const char *path, const char *mode) { return fopen(path, mode); }
static void hook(const char *line)
{
    hooks++;
    snprintf(last_line, sizeof(last_line), "%s", line);
}
static void legacy_va(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    /* Parenthesized name checks the callable ABI as well as the macro. */
    (pclog_ex)(fmt, ap);
    va_end(ap);
}
static void device_va(void *log, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_out(log, fmt, ap);
    va_end(ap);
}
static void cyclic_va(void *log, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_out_cyclic(log, fmt, ap);
    va_end(ap);
}
int main(void)
{
    FILE *fp = tmpfile();
    CHECK(fp != NULL);
    if (!fp) return 1;
    stdlog = fp;
    pclog_hook = hook;
    int side_effect = 0;
    pclog("macro %d\n", ++side_effect);
    (pclog)("ABI %d\n", 7);
    legacy_va("VA %d\n", 8);
    void *log = log_open("device");
    void *cyclic = log_open_cyclic("cyclic");
    log_set_suppr_seen(log, 0);
    device_va(log, "device line %d\n", 9);
    cyclic_va(cyclic, "cyclic line %d\n", 10);
    fflush(fp);
#ifdef RELEASE_BUILD
    CHECK(side_effect == 0);
    CHECK(hooks == 0);
    CHECK(ftell(fp) == 0);
#else
    CHECK(side_effect == 1);
    CHECK(hooks == 3);
    CHECK(ftell(fp) > 0);
    CHECK(!strcmp(last_line, "VA 8\n"));
#endif
    /* Explicit CLI/status output remains available in either build. */
    unsigned prior = hooks;
    always_log("status %d\n", 11);
    CHECK(hooks == prior + 1);
    CHECK(!strcmp(last_line, "status 11\n"));
    CHECK(ftell(fp) > 0);
    log_close(log);
    log_close(cyclic);
    pclog_hook = NULL;
    stdlog = NULL;
    fclose(fp);
    /* A release diagnostic must not create or open the configured log file. */
#ifdef RELEASE_BUILD
    snprintf(log_path, sizeof(log_path), "%s", "logging-test-must-not-be-created.log");
    (pclog)("quiet\n");
    CHECK(stdlog == NULL);
    log_path[0] = '\0';
#endif
    printf("Logging %s: %u checks, %u failures\n",
#ifdef RELEASE_BUILD
           "release",
#else
           "debug",
#endif
           checks, failures);
    return failures ? 1 : 0;
}
