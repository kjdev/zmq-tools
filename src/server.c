#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <syslog.h>
#include <signal.h>
#include <spawn.h>

#include <zmq.h>

#include "config.h"
#include "utils.h"

int zt_msgno_ = 0;
char *zt_msgfile_ = NULL;

static int interrupted_ = 0;

typedef struct {
    int init;
    pid_t pid;
    int in[2];
    int out[2];
    posix_spawn_file_actions_t actions;
} zt_spawn_t;

static void
zt_signal_handler(int sig)
{
    interrupted_ = 1;
}

static void
zt_signals(void)
{
    struct sigaction sa;
    sa.sa_handler = zt_signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

#define zt_spawn_pipeclose(_fd) \
    if (_fd != -1) {            \
        close(_fd);             \
        _fd = -1;               \
    }

static int
zt_spawn_init(zt_spawn_t *spawn, const char *script, int type)
{
    spawn->init = 0;
    spawn->pid = -1;
    spawn->in[0] = -1;
    spawn->in[1] = -1;
    spawn->out[0] = -1;
    spawn->out[1] = -1;

    if (!script) {
        return 1;
    }

    if (posix_spawn_file_actions_init(&spawn->actions) != 0) {
        zt_error("POSIX spawn file action initialize\n");
        return 1;
    }
    spawn->init = 1;

    /* stdin */
    if (pipe(spawn->in) == -1) {
        zt_error("POSIX spawn input pipe\n");
        posix_spawn_file_actions_destroy(&spawn->actions);
        return 1;
    }

    if (posix_spawn_file_actions_addclose(&spawn->actions, spawn->in[1]) != 0 ||
        posix_spawn_file_actions_adddup2(&spawn->actions, spawn->in[0],
                                         STDIN_FILENO) != 0) {
        zt_error("POSIX spawn stdin\n");
        zt_spawn_pipeclose(spawn->in[0]);
        zt_spawn_pipeclose(spawn->in[1]);
        zt_spawn_pipeclose(spawn->out[0]);
        zt_spawn_pipeclose(spawn->out[1]);
        posix_spawn_file_actions_destroy(&spawn->actions);
        return 1;
    }

    /* stdout: zmq: REP STREAM */
    if (type == ZMQ_REP || type == ZMQ_STREAM) {
        if (pipe(spawn->out) == -1) {
            zt_error("POSIX spawn output pipe\n");
            posix_spawn_file_actions_destroy(&spawn->actions);
            return 1;
        }

        if (posix_spawn_file_actions_addclose(&spawn->actions,
                                              spawn->out[0]) != 0 ||
            posix_spawn_file_actions_adddup2(&spawn->actions, spawn->out[1],
                                             STDOUT_FILENO) != 0) {
            zt_error("POSIX spawn stdout\n");
            zt_spawn_pipeclose(spawn->in[0]);
            zt_spawn_pipeclose(spawn->in[1]);
            zt_spawn_pipeclose(spawn->out[0]);
            zt_spawn_pipeclose(spawn->out[1]);
            posix_spawn_file_actions_destroy(&spawn->actions);
            return 1;
        }
    }

    if (posix_spawnp(&spawn->pid, script, &spawn->actions,
                     NULL, NULL, NULL) != 0) {
        zt_error("POSIX spawn run\n");
        zt_spawn_pipeclose(spawn->in[0]);
        zt_spawn_pipeclose(spawn->in[1]);
        zt_spawn_pipeclose(spawn->out[0]);
        zt_spawn_pipeclose(spawn->out[1]);
        posix_spawn_file_actions_destroy(&spawn->actions);
        spawn->pid = -1;
        return 1;
    }

    zt_spawn_pipeclose(spawn->in[0]);
    zt_spawn_pipeclose(spawn->out[1]);

    return 0;
}

static void
zt_spawn_destroy(zt_spawn_t *spawn)
{
    zt_spawn_pipeclose(spawn->in[0]);
    zt_spawn_pipeclose(spawn->in[1]);
    zt_spawn_pipeclose(spawn->out[0]);
    zt_spawn_pipeclose(spawn->out[1]);

    if (spawn->pid) {
        int rc;
        if (interrupted_) {
            kill(spawn->pid, SIGTERM);
        }
        waitpid(spawn->pid, &rc, 0);
    }

    if (spawn->init) {
        posix_spawn_file_actions_destroy(&spawn->actions);
    }
}

static void
zt_spawn_write(zt_spawn_t *spawn, const void *buf, size_t len)
{
    zt_debug("write: [%ld] \"%.*s\"\n", len, (int)len, (char *)buf);
    write(spawn->in[1], buf, len);
}

static void
zt_spawn_write_delimiter(zt_spawn_t *spawn, const void *buf)
{
    char delim[2] = { '\n', '\t' };
    size_t len = strlen(buf);
    if (strncasecmp(buf, "\\n", len) == 0) {
        zt_spawn_write(spawn, &delim[0], 1);
    } else if (strncasecmp(buf, "\\t", len) == 0) {
        zt_spawn_write(spawn, &delim[1], 1);
    } else {
        zt_spawn_write(spawn, buf, len);
    }
}

static char *
zt_spawn_read(zt_spawn_t *spawn, size_t *len)
{
    int bufsize, block, size;
    char *buf = NULL;

    bufsize = block = size = BUFSIZ;
    *len = 0;

    buf = (char *)malloc(bufsize + 1);
    if (!buf) {
        zt_error("read buffer allocate\n");
        return NULL;
    }

    memset(buf, 0, bufsize + 1);

    zt_spawn_pipeclose(spawn->in[1]);

    if (!interrupted_ && spawn->out[0] != -1) {
        while (1) {
            ssize_t byte;
            void *tmp;

            byte = read(spawn->out[0], &buf[*len], size);
            *len += byte;
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
                zt_error("read buffer allocate\n");
                break;
            }

            buf = (char *)tmp;
        }

        zt_spawn_pipeclose(spawn->out[0]);
    }

    if (*len == 0) {
        free(buf);
        return NULL;
    }

    return buf;
}

static void
usage(char *arg)
{
    char *command = basename(arg);

    printf("Usage: %s -e <ENDPOINT> -t <TYPE> [-s SUBSCRIBE]\n", command);
    printf("%*s        [-S <SCRIPT> [-d <DELIMITER>]]\n",
           (int)strlen(command), "");
    printf("%*s        [-D <COMMAND> [-p <FILE>]] [-l <FILE>]\n\n",
           (int)strlen(command), "");
    printf("  -e, --endpoint=ENDPOINT   ZeroMQ socket endpoint\n");
    printf("  -t, --type=TYPE           ZeroMQ type [PULL|SUB|REP|STREAM]\n");
    printf("  -s, --subscribe=SUBSCRIBE "
           "ZeroMQ subscribe key [type=SUB] <default: none>\n");
    printf("  -S, --script=SCRIPT       script to run after receive\n");
    printf("  -d, --delimiter=DELIMITER "
           "delimiter when sending the script <default: none>\n");
    printf("  -D, --daemon=COMMAND      daemon command [start|stop]\n");
    printf("  -p, --pidfile=FILE        pid file <default: %s>\n",
           ZMQ_TOOLS_PIDFILE);
    printf("  -l, --log=FILE            log file <default: none(stderr)>\n");
    printf("\n");
}

int
main(int argc, char **argv)
{
    int opt, origin_umask;
    void *context, *socket;

    int type = 0;
    char *endpoint = NULL;
    char *typename = NULL;
    char *subscribe = NULL;
    char *delimiter = NULL;
    char *daemonize = NULL;
    char *pidfile = ZMQ_TOOLS_PIDFILE;
    char *script = NULL;

    const struct option opts[] = {
        { "endpoint", 1, NULL, 'e' },
        { "type", 1, NULL, 't' },
        { "subscribe", 1, NULL, 's' },
        { "script", 1, NULL, 'S' },
        { "delimiter", 1, NULL, 'd' },
        { "pidfile", 1, NULL, 'p' },
        { "daemon", 1, NULL, 'D' },
        { "log", 1, NULL, 'l' },
        { "verbose", 1, NULL, 'v' },
        { "quiet", 0, NULL, 'q' },
        { "version", 0, NULL, 'V' },
        { "help", 0, NULL, 'h' },
        { NULL, 0, NULL, 0 },
    };

    zt_msgno_ = 0;
    zt_msgfile_ = NULL;

    while ((opt = getopt_long(argc, argv, "e:t:s:d:S:p:D:l:vqVh",
                              opts, NULL)) != -1) {
        switch (opt) {
            case 'e':
                endpoint = optarg;
                break;
            case 't':
                typename = optarg;
                if (strcasecmp(typename, "PULL") == 0) {
                    type = ZMQ_PULL;
                } else if (strcasecmp(typename, "SUB") == 0) {
                    type = ZMQ_SUB;
                } else if (strcasecmp(typename, "REP") == 0) {
                    type = ZMQ_REP;
                } else if (strcasecmp(typename, "STREAM") == 0) {
                    type = ZMQ_STREAM;
                }
                break;
            case 's':
                subscribe = optarg;
                break;
            case 'd':
                delimiter = optarg;
                break;
            case 'S':
                script = optarg;
                break;
            case 'p':
                pidfile = optarg;
                break;
            case 'D':
                daemonize = optarg;
                break;
            case 'l':
                zt_msgfile_ = optarg;
                break;
            case 'v':
                if (optarg) {
                    zt_msgno_ = atoi(optarg);
                } else {
                    zt_msgno_ += 1;
                }
                break;
            case 'q':
                zt_msgno_ = -1;
                break;
            case 'V':
                zt_version_print(basename(argv[0]));
                return 0;
            case 'H':
            default:
                usage(argv[0]);
                return 1;
        }
    }

    /* check arguments */
    if (!endpoint || strlen(endpoint) == 0) {
        usage(argv[0]);
        zt_error("no such endpoint\n");
        return 1;
    }

    if (type == 0) {
        usage(argv[0]);
        zt_error("no such type\n");
        return 1;
    }

    if (script) {
        struct stat st;
        if (stat(script, &st) != 0) {
            zt_error("no such script file\n");
            return 1;
        }

        if ((S_IXUSR & st.st_mode) ||
            (S_IXOTH & st.st_mode) ||
            (S_IXGRP & st.st_mode)) {
            FILE *file = fopen(script, "rb");
            if (!file) {
                zt_error("command open file: %s\n", script);
                return 1;
            }
            fclose(file);
        } else {
            zt_error("command not execute file: %s\n", script);
            return 1;
        }
    }

    /* daemonize */
    if (daemonize) {
        pid_t pid;
        FILE *file = NULL;

        if (!pidfile || strlen(pidfile) == 0) {
            usage(argv[0]);
            zt_error("no such pidfile\n");
            return 1;
        }
        zt_verbose_ex(2, "PidFile: %s\n", pidfile);

        if (strcasecmp(daemonize, "start") == 0) {
            int nochdir = 1, noclose = 0;
            openlog("zmq-server", LOG_PID, LOG_DAEMON);
            if (daemon(nochdir, noclose) == -1) {
                syslog(LOG_INFO, "Failed to zmq-server\n");
                zt_error("failed to zmq-server start\n");
                return 1;
            }
            syslog(LOG_INFO, "zmq-server startted\n");
            pid = getpid();
            file = fopen(pidfile, "w");
            if (file != NULL) {
                fprintf(file, "%d\n", pid);
                fclose(file);
            } else {
                syslog(LOG_INFO,
                       "Failed to record process id to file: %d\n", pid);
            }
        } else if (strcasecmp(daemonize, "stop") == 0) {
            openlog("zmq-server", LOG_PID, LOG_DAEMON);
            file = fopen(pidfile, "r");
            if (file != NULL) {
                fscanf(file, "%d\n", &pid);
                fclose(file);
                unlink(pidfile);
                if (kill(pid, SIGTERM) == 0) {
                    syslog(LOG_INFO, "zmq-server stopped\n");
                }
                return 0;
            }
            return 1;
        } else {
            zt_error("no such daemon command\n");
            return 1;
        }
    }

    /* server */
    context = zmq_ctx_new();
    if (!context) {
        zt_error("ZeroMQ context: %s\n", zmq_strerror(errno));
        return 1;
    }

    socket = zmq_socket(context, type);
    if (!socket) {
        zt_error("ZeroMQ socket: %s\n", zmq_strerror(errno));
        zmq_ctx_destroy(context);
        return 1;
    }

    if (type == ZMQ_SUB) {
        if (!subscribe) {
            subscribe = "";
        }

        if (zmq_setsockopt(socket, ZMQ_SUBSCRIBE,
                           subscribe, strlen(subscribe)) < 0) {
            zt_error("ZeroMQ socket option subscribe\n");
            zmq_close(socket);
            zmq_ctx_destroy(context);
            return 11;
        }

        zt_verbose("ZeroMQ subscribe: \"%s\"\n", subscribe);
    }

    zt_verbose("ZeroMQ socket: %s\n", typename);

    origin_umask = umask(0);
    if (zmq_bind(socket, endpoint) < 0) {
        zt_error("ZeroMQ bind: %s: %s\n", endpoint, zmq_strerror(errno));
        zmq_close(socket);
        zmq_ctx_destroy(context);
        umask(origin_umask);
        return 1;
    }
    umask(origin_umask);

    zt_verbose("ZeroMQ bind: %s\n", endpoint);

    /* main loop */
    zt_verbose_ex(2, "ZeroMQ start server\n");

    zt_signals();

    if (!interrupted_) {
        zmq_pollitem_t pollitems[] = { { socket, 0, ZMQ_POLLIN, 0 } };

        while (!interrupted_) {
            if (zmq_poll(pollitems, 1, -1) < 0) {
                break;
            }

            if (pollitems[0].revents & ZMQ_POLLIN) {
                zt_spawn_t zspawn;
                size_t frame = 0;
                uint8_t identity[256] = { 0, };
                size_t identity_size = sizeof(identity);

                zt_spawn_init(&zspawn, script, type);

                while (!interrupted_) {
                    int flags = 0;
                    size_t size = 0;
                    void *data = NULL;
                    zmq_msg_t msg;

                    if (zmq_msg_init(&msg) != 0) {
                        break;
                    }

                    if (zmq_recvmsg(socket, &msg, 0) < 0) {
                        zt_error("ZeroMQ receive: %s\n", zmq_strerror(errno));
                        zmq_msg_close(&msg);
                        break;
                    }

                    if (zmq_msg_more(&msg)) {
                        flags = ZMQ_RCVMORE;
                    }

                    size = zmq_msg_size(&msg);
                    data = zmq_msg_data(&msg);

                    zt_debug("recv(%d): [%ld] \"%.*s\"\n",
                             frame, size, (int)size, (char *)data);

                    if (type != ZMQ_STREAM) {
                        /* zmq: PULL SUB REP */
                        if (zspawn.pid != -1) {
                            zt_spawn_write(&zspawn, data, size);
                            if (flags == ZMQ_RCVMORE && delimiter) {
                                zt_spawn_write_delimiter(&zspawn, delimiter);
                            }
                        }
                    } else {
                        /* zmq: STREAM */
                        if (frame == 0) {
                            identity_size = size;
                            memcpy(identity, data, size);
                        } else if (zspawn.pid != -1) {
                            zt_spawn_write(&zspawn, data, size);
                            if (flags == ZMQ_RCVMORE && delimiter) {
                                zt_spawn_write_delimiter(&zspawn, delimiter);
                            }
                        }
                    }

                    zmq_msg_close(&msg);

                    if (flags == 0) {
                        break;
                    }

                    ++frame;
                }

                if (type == ZMQ_REP) {
                    /* zmq: REP */
                    char *buf = NULL;
                    size_t len = 0;

                    if (zspawn.pid != -1) {
                        buf = zt_spawn_read(&zspawn, &len);
                    }

                    if (buf) {
                        zt_debug("reply: [%ld] \"%.*s\"\n",
                                   len, (int)len, buf);
                        if (zmq_send(socket, buf, len, 0) < 0) {
                            zt_error("ZeroMQ reply: %s\n", zmq_strerror(errno));
                        }
                        free(buf);
                    } else {
                        char nil[1] = { '\0' };
                        if (zmq_send(socket, nil, 1, 0) < 0) {
                            zt_error("ZeroMQ send: %s\n", zmq_strerror(errno));
                        }
                    }
                } else if (type == ZMQ_STREAM) {
                    /* zmq: STREAM */
                    char *buf = NULL;
                    size_t len = 0;

                    if (zmq_send(socket, identity,
                                 identity_size, ZMQ_SNDMORE) < 0) {
                        zt_error("ZeroMQ send: %s\n", zmq_strerror(errno));
                    }

                    if (zspawn.pid != -1) {
                        buf = zt_spawn_read(&zspawn, &len);
                    }

                    if (buf) {
                        zt_debug("response: [%ld] \"%.*s\"\n",
                                 len, (int)len, buf);
                        if (zmq_send(socket, buf, len, 0) < 0) {
                            zt_error("ZeroMQ send: %s\n", zmq_strerror(errno));
                        }
                        free(buf);
                    } else {
                        char nil[1] = { '\0' };
                        if (zmq_send(socket, nil, 1, 0) < 0) {
                            zt_error("ZeroMQ send: %s\n", zmq_strerror(errno));
                        }
                    }
                }

                zt_spawn_destroy(&zspawn);
            }
        }
    }

    zt_verbose_ex(2, "ZeroMQ end server\n");

    /* cleanup */
    zmq_close(socket);
    zmq_ctx_destroy(context);

    return 0;
}
