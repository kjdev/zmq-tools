#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <syslog.h>

#include <zmq.h>

#include "config.h"
#include "utils.h"

int zt_msgno_ = 0;
char *zt_msgfile_ = NULL;

static void
zt_sockets_destroy(void **sockets, int n)
{
    if (sockets) {
        int i = 0;
        do {
            if (sockets[i]) {
                zmq_close(sockets[i]);
            }
            ++i;
        } while (i < n);
        free(sockets);
    }
}

static int
zt_send(int type, void *socket, void *buf, size_t len,
          uint8_t *identity, size_t identity_size)
{
    switch (type) {
        case ZMQ_PUSH:
        case ZMQ_XPUB:
            zt_debug("send: [%ld] \"%.*s\"\n", len, (int)len, buf);
            if (zmq_send(socket, buf, len, 0) < 0) {
                zt_error("ZeroMQ send: %s\n", zmq_strerror(errno));
                return 1;
            }
            return 0;
        case ZMQ_REQ: {
            zmq_msg_t msg;
            zt_debug("send: [%ld] \"%.*s\"\n", len, (int)len, buf);
            if (zmq_send(socket, buf, len, 0) < 0) {
                zt_error("ZeroMQ send: %s\n", zmq_strerror(errno));
                return 1;
            }
            /* reply */
            if (zmq_msg_init(&msg) != 0) {
                zt_error("ZeroMQ msg: %s\n", zmq_strerror(errno));
                return 1;
            }
            if (zmq_recvmsg(socket, &msg, 0) >= 0) {
                size_t size = zmq_msg_size(&msg);
                const char *data = (char *)zmq_msg_data(&msg);
                zt_verbose("reply: [%ld] \"%.*s\"\n", size, (int)size, data);
            } else {
                zt_error("ZeroMQ receive: %s\n", zmq_strerror(errno));
            }
            zmq_msg_close(&msg);
            return 0;
        }
        case ZMQ_STREAM: {
            zmq_msg_t msg;
            zt_debug("send: [%ld] (identity)\n", identity_size);
            if (zmq_send(socket, identity, identity_size, ZMQ_SNDMORE) < 0) {
                zt_error("ZeroMQ stream send identity: %s\n",
                         zmq_strerror(errno));
                return 1;
            }
            zt_debug("send: [%ld] \"%.*s\"\n", len, (int)len, buf);
            if (zmq_send(socket, buf, len, 0) < 0) {
                zt_error("ZeroMQ stream send: %s\n", zmq_strerror(errno));
                return 1;
            }
            /* response */
            if (zmq_msg_init(&msg) != 0) {
                zt_error("ZeroMQ msg: %s\n", zmq_strerror(errno));
                return 1;
            }
            if (zmq_recvmsg(socket, &msg, 0) < 0) {
                zt_error("ZeroMQ receive: %s\n", zmq_strerror(errno));
            }
            zt_debug("recv: (identity)\n");
            zmq_msg_close(&msg);

            if (zmq_msg_init(&msg) != 0) {
                zt_error("ZeroMQ msg: %s\n", zmq_strerror(errno));
                return 1;
            }
            if (zmq_recvmsg(socket, &msg, 0) >= 0) {
                size_t size = zmq_msg_size(&msg);
                const char *data = (char *)zmq_msg_data(&msg);
                zt_verbose("response: [%ld] \"%.*s\"\n", size, (int)size, data);
            } else {
                zt_error("ZeroMQ receive: %s\n", zmq_strerror(errno));
            }
            zmq_msg_close(&msg);
            return 0;
        }
        default:
            zt_error("ZeroMQ unsupported type\n");
            return 1;
    }
    return 1;
}

static void
usage(char *arg)
{
    char *command = basename(arg);

    printf("Usage: %s -e <ENDPOINT> -t <TYPE> [-l <FILE>] MESSAGE ...\n\n",
           command);
    printf("  -e, --endpoint=ENDPOINT ZeroMQ socket endpoint\n");
    printf("  -t, --type=TYPE         ZeroMQ type [PUSH|PUB|REQ|STREAM]\n");
    printf("  -l, --log=FILE          log file <default: none(stderr)>\n");
}

int
main(int argc, char **argv)
{
    int i, s, opt;
    char *token, *tmp, *in = NULL;
    void *context, **sockets;

    int socket = 0, conn = 0, type = 0, timeo = 1000;
    char *endpoint = NULL, *typename = NULL;

    uint8_t identity[256] = { 0, };
    size_t size, identity_size = sizeof(identity);

    const struct option opts[] = {
        { "endpoint", 1, NULL, 'e' },
        { "type", 1, NULL, 't' },
        { "log", 1, NULL, 'l' },
        { "verbose", 1, NULL, 'v' },
        { "quiet", 0, NULL, 'q' },
        { "version", 0, NULL, 'V' },
        { "help", 0, NULL, 'h' },
        { NULL, 0, NULL, 0 },
    };

    zt_msgno_ = 0;
    zt_msgfile_ = NULL;

    while ((opt = getopt_long(argc, argv, "e:t:l:vqVh", opts, NULL)) != -1) {
        switch (opt) {
            case 'e':
                endpoint = optarg;
                break;
            case 't':
                typename = optarg;
                if (strcasecmp(typename, "PUSH") == 0) {
                    type = ZMQ_PUSH;
                } else if (strcasecmp(typename, "PUB") == 0) {
                    type = ZMQ_XPUB;
                } else if (strcasecmp(typename, "REQ") == 0) {
                    type = ZMQ_REQ;
                } else if (strcasecmp(typename, "STREAM") == 0) {
                    type = ZMQ_STREAM;
                }
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

    opt = argc - optind;
    if (opt <= 0) {
        if (zt_stdin()) {
            /* stdin */
            in = zt_stdin_read();
        }
        if (!in) {
            usage(argv[0]);
            zt_error("no such message\n");
            return 1;
        }
    }

    /* connect count */
    i = 0;
    while (1) {
        ++conn;
        token = strchr(endpoint + i, ';');
        if (token == NULL) {
            break;
        }
        i = (++token) - endpoint;
    }

    zt_verbose_ex(2, "connection: %d\n", conn);

    /* zmq: context */
    context = zmq_ctx_new();
    if (!context) {
        zt_error("ZeroMQ context: %s\n", zmq_strerror(errno));
        return 1;
    }

    /* zmq: socket */
    if (type != ZMQ_XPUB) {
        /* zmq: PUSH REQ STREAM */
        socket = 1;
    } else {
        /* zmq: XPUB */
        socket = conn;
    }

    size = sizeof(void *) * socket;
    sockets = malloc(size);
    if (!sockets) {
        zt_error("memory allocate\n");
        return 1;
    }
    memset(sockets, 0, size);

    zt_verbose("ZeroMQ sockets: %s\n", typename);

    s = 0;
    do {
        sockets[s] = zmq_socket(context, type);
        if (!sockets[s]) {
            zt_error("ZeroMQ socket: %s\n", zmq_strerror(errno));
            zt_sockets_destroy(sockets, socket);
            zmq_ctx_destroy(context);
            return 1;
        }

        zmq_setsockopt(sockets[s], ZMQ_LINGER, &timeo, sizeof(timeo));
        zmq_setsockopt(sockets[s], ZMQ_SNDTIMEO, &timeo, sizeof(timeo));
        zmq_setsockopt(sockets[s], ZMQ_RCVTIMEO, &timeo, sizeof(timeo));

        if (type != ZMQ_STREAM) {
            /* zmq: PUSH REQ XPUB */
            int immediate = 1;
            zmq_setsockopt(sockets[s], ZMQ_IMMEDIATE,
                           &immediate, sizeof(immediate));
        }

        ++s;
    } while (s < socket);

    /* zmq: connect */
    tmp = strdup(endpoint);
    token = tmp;

    i = 0;
    do {
        int rc;
        char *connect = strtok(token, ";");
        if (connect == NULL) {
            break;
        }

        if (type != ZMQ_XPUB) {
            /* zmq: PUSH REQ STREAM */
            rc = zmq_connect(sockets[0], connect);
        } else {
            /* zmq: XPUB */
            rc = zmq_connect(sockets[i], connect);
        }

        if (rc == -1) {
            zt_error("ZeroMQ connect: %s: %s\n", connect, zmq_strerror(errno));
            zt_sockets_destroy(sockets, socket);
            zmq_ctx_destroy(context);
            free(tmp);
            return 1;
        }

        zt_verbose("ZeroMQ connect: %s\n", connect);

        ++i;
        token = NULL;
    } while (i < conn);

    free(tmp);

    if (type == ZMQ_XPUB) {
        /* zmq: XPUB */
        s = 0;
        do {
            zmq_msg_t msg;
            if (zmq_msg_init(&msg) != 0) {
                zt_error("ZeroMQ msg: %s\n", zmq_strerror(errno));
                zt_sockets_destroy(sockets, socket);
                zmq_ctx_destroy(context);
                return 1;
            }
            if (zmq_recvmsg(sockets[s], &msg, 0) >= 0) {
                if (zmq_msg_size(&msg) <= 0) {
                    zt_error("ZeroMQ subscription: %d\n", i);
                }
            } else {
                zt_error("ZeroMQ receive: %s\n", zmq_strerror(errno));
            }
            zmq_msg_close(&msg);
            ++s;
        } while (s < socket);
    } else if (type == ZMQ_STREAM) {
        /* zmq: STREAM */
        if (zmq_getsockopt(sockets[0], ZMQ_IDENTITY,
                           identity, &identity_size) != 0) {
            zt_error("ZeroMQ stream identity: %s\n", zmq_strerror(errno));
            zt_sockets_destroy(sockets, socket);
            zmq_ctx_destroy(context);
            return 1;
        }
    }

    zt_verbose_ex(2, "ZeroMQ message sending\n");

    if (!in) {
        /* sending: argument */
        i = optind;
        opt = argc - 1;
        while (i < opt) {
            s = 0;
            do {
                if (zt_send(type, sockets[s], argv[i], strlen(argv[i]),
                            identity, identity_size) != 0) {
                    zt_sockets_destroy(sockets, socket);
                    zmq_ctx_destroy(context);
                    return 1;
                }
                ++s;
            } while (s < socket);
            ++i;
        }

        s = 0;
        do {
            if (zt_send(type, sockets[s], argv[opt], strlen(argv[opt]),
                        identity, identity_size) != 0) {
                zt_sockets_destroy(sockets, socket);
                zmq_ctx_destroy(context);
                return 1;
            }
            ++s;
        } while (s < socket);
    } else {
        /* stdin: sending */
        s = 0;
        do {
            if (zt_send(type, sockets[s], in, strlen(in),
                        identity, identity_size) != 0) {
                zt_sockets_destroy(sockets, socket);
                zmq_ctx_destroy(context);
                return 1;
            }
            ++s;
        } while (s < socket);
        free(in);
    }

    /* cleanup */
    zt_sockets_destroy(sockets, socket);
    zmq_ctx_destroy(context);

    return 0;
}
