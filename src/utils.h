#ifndef __ZMQ_TOOLS_UTILS_H__
#define __ZMQ_TOOLS_UTILS_H__

#include <stdio.h>
#include <libgen.h>

extern int zt_msgno_;
extern char *zt_msgfile_;

void zt_version_print(const char *app);

int zt_stdin(void);
char * zt_stdin_read(void);

FILE * zt_msg_init(const char *prefix);
void zt_msg_destroy(FILE *stream, const char *suffix);
void zt_msg_print(FILE *stream, const char *format, ...);
void zt_msg_echo(FILE *stream, const char *format, ...);

#define zt_echo(...) zt_msg_echo(zt_msg_init(NULL), __VA_ARGS__)

#ifdef NDEBUG
#define zt_error(...) \
    if (zt_msgno_ >= 0) { zt_echo("ERROR: "__VA_ARGS__); }
#define zt_warning(...) \
    if (zt_msgno_ >= 0) { zt_echo("WARNING: "__VA_ARGS__); }
#define zt_verbose(...) \
    if (zt_msgno_ > 0) { zt_echo("INFO: "__VA_ARGS__); }
#define zt_verbose_ex(_level, ...) \
    if (zt_msgno_ >= _level) { zt_echo("INFO: "__VA_ARGS__); }
#define zt_debug(...)
#else
#define zt_error(...)                             \
    if (zt_msgno_ >= 0) {                         \
        FILE *file = zt_msg_init("\e[31m");       \
        zt_msg_print(file, "ERROR: "__VA_ARGS__); \
        zt_msg_destroy(file, "\e[m");             \
    }
#define zt_warning(...)                             \
    if (zt_msgno_ >= 0) {                           \
        FILE *file = zt_msg_init("\e[33m");         \
        zt_msg_print(file, "WARNING: "__VA_ARGS__); \
        zt_msg_destroy(file, "\e[m");               \
    }
#define zt_verbose(...) \
    if (zt_msgno_ > 0) { zt_echo("INFO: "__VA_ARGS__); }
#define zt_verbose_ex(_level, ...)                \
    if (zt_msgno_ >= _level) {                    \
        FILE *file = zt_msg_init(NULL);           \
        zt_msg_print(file, "INFO(%d): ", _level); \
        zt_msg_print(file, __VA_ARGS__);          \
        zt_msg_destroy(file, NULL);               \
    }
#define zt_debug(...)                                                 \
    if (zt_msgno_ > 0) {                                              \
        FILE *file = zt_msg_init("\e[30;1m");                         \
        zt_msg_print(file, "\e[35m%s\e[36m:\e[32m%d\e[36m:\e[30;1m ", \
                     basename(__FILE__), __LINE__);                   \
        zt_msg_print(file, __VA_ARGS__);                              \
        zt_msg_destroy(file, "\e[m");                                 \
    }
#endif

#endif
