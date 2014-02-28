#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <stdarg.h>

#include "utils.h"
#include "config.h"

void
zt_version_print(const char *app)
{
    printf("%s version: %d.%d.%d\n",
           app,
           ZMQ_TOOLS_VERSION_MAJOR,
           ZMQ_TOOLS_VERSION_MINOR,
           ZMQ_TOOLS_VERSION_BUILD);
}

int
zt_stdin(void)
{
    struct stat st;
    if (fstat(STDIN_FILENO, &st) == 0 && S_ISFIFO(st.st_mode)) {
        return 1;
    }
    return 0;
}

char *
zt_stdin_read(void)
{
    char *buf = NULL;
    size_t bufsize, block, size, len = 0;

    bufsize = block = size = BUFSIZ;

    buf = (char *)malloc(bufsize + 1);
    if (!buf) {
        zt_error("memory allocate\n");
        return NULL;
    }
    memset(buf, 0, bufsize + 1);

    while (1) {
        ssize_t byte;
        void *tmp;

        byte = read(STDIN_FILENO, &buf[len], size);
        len += byte;
        if (byte == 0) {
            break;
        }

        size -= byte;
        if (size > 0) {
            continue;
        }

        bufsize += block;
        size = block;

        tmp = realloc(buf, bufsize + 1);
        if (!tmp) {
            zt_error("memory allocate\n");
            free(buf);
            return NULL;
        }

        buf = (char *)tmp;
    }

    if (len == 0) {
        free(buf);
        return NULL;
    }

    buf[len] = '\0';

    return buf;
}

FILE *
zt_msg_init(const char *prefix)
{
    FILE *file = NULL;

    if (zt_msgfile_) {
        file = fopen(zt_msgfile_, "ab+");
        if (file) {
            flock(fileno(file), LOCK_EX);
        } else {
            fprintf(stderr, "open file: %s\n", zt_msgfile_);
            file = stderr;
        }
    } else {
        file = stderr;
    }

    if (prefix) {
        fprintf(file, "%s", prefix);
    }

    return file;
}

void
zt_msg_destroy(FILE *stream, const char *suffix)
{
    if (stream) {
        if (suffix) {
            fprintf(stream, "%s", suffix);
        }
        fflush(stream);

        if (stream != stderr) {
            flock(fileno(stream), LOCK_UN);
            fclose(stream);
        }
    }
}

void
zt_msg_print(FILE *stream, const char *format, ...)
{
    va_list ap;

    if (!stream) {
        return;
    }

    va_start(ap, format);

    vfprintf(stream, format, ap);

    va_end(ap);
}

void
zt_msg_echo(FILE *stream, const char *format, ...)
{
    va_list ap;

    if (!stream) {
        return;
    }

    va_start(ap, format);

    vfprintf(stream, format, ap);

    va_end(ap);

    zt_msg_destroy(stream, NULL);
}
