#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define PERROR()                                        \
    do {                                                \
        char buf[512];                                  \
        snprintf(buf, sizeof buf, "%d", __LINE__);      \
        perror(buf);                                    \
    } while (0);

#define CONNS_MAX 32

typedef struct {
    int fd;

    char rbuf[1024];
    size_t rbuflen;
    size_t rbufrem;
    char *rbufp;

    char sbuf[1024];
    size_t sbuflen;
    size_t sbufrem;
    char *sbufp;
} conn_t;

static inline void swap_char(char *a, char *b) {
    char c = *a;
    *a = *b;
    *b = c;
}

void reverse_string(char *buf, size_t buflen) {
    size_t i2 = buflen - 1;
    for (size_t i1 = 0; i1 < buflen / 2; i1 += 1) {
        swap_char(&buf[i1], &buf[i2]);
        i2 -= 1;
    }
}

int conn_recv(conn_t *conn)
{
    char *org_rbufp = conn->rbufp;
    ssize_t len = recv(conn->fd, conn->rbufp, conn->rbufrem, 0);
    if (len <= 0) {
        shutdown(conn->fd, SHUT_RDWR);
        close(conn->fd);
        return -1;
    }

    conn->rbufp += len;
    conn->rbuflen += len;
    conn->rbufrem -= len;
    *(conn->rbufp) = '\0';

    // In case \r\n is truncated.
    char *eol;
    while ((eol = strstr((org_rbufp == conn->rbuf
                          ? conn->rbuf : (org_rbufp - 1)), "\r\n")) != NULL)
    {
        size_t msglen = eol + 2 - conn->rbuf;
        if (msglen > conn->sbufrem) {
            // Next message won't fit in send buffer. Don't receive anymore.
            return -2;
        } else {
            memcpy(conn->sbufp, conn->rbuf, msglen);
            reverse_string(conn->sbufp, msglen - 2);
            conn->sbufp += msglen;
            conn->sbuflen += msglen;
            conn->sbufrem -= msglen;

            // TODO: Make this a ring buffer so we don't have to memmove().
            memmove(conn->rbuf, eol + 2, conn->rbuflen - msglen);
            conn->rbufp -= msglen;
            conn->rbuflen -= msglen;
            conn->rbufrem += msglen;
            *(conn->rbufp) = '\0';

            org_rbufp = conn->rbuf;
        }
    }

    return len;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("give a port number\n");
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock == -1) {
        PERROR();
        return 2;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(atoi(argv[1])),
        .sin_addr = {
            .s_addr = htonl(0)
        }
    };

    int ret = bind(sock, (struct sockaddr*)&addr, sizeof addr);
    if (ret == -1) {
        PERROR();
        return 3;
    }

    ret = listen(sock, CONNS_MAX);
    if (ret == -1) {
        PERROR();
        return 4;
    }

    fd_set active_rfds, active_wfds;
    FD_ZERO(&active_rfds);
    FD_ZERO(&active_wfds);
    FD_SET(sock, &active_rfds);

    int fd_max = sock;
    conn_t conns[CONNS_MAX];
    for (uint8_t i = 0; i < CONNS_MAX; ++i) {
        conns[i].fd = -1;
    }

    conn_t *empty_conn = &conns[0];

    fd_set rfds;
    fd_set wfds;

    for (;;) {
        rfds = active_rfds;
        wfds = active_wfds;

        ret = select(fd_max + 1, &rfds, &wfds, NULL, NULL);
        if (ret == -1) {
            PERROR();
            break;
        }

        fd_max = sock;

        if (FD_ISSET(sock, &rfds)) {
            if (empty_conn != NULL) {
                ret = accept(sock, NULL, NULL);
                if (ret == -1) {
                    PERROR();
                } else {
                    /* printf("new connection: %d\n", ret); */
                    FD_SET(ret, &active_rfds);
                    fd_max = MAX(fd_max, ret);

                    empty_conn->fd = ret;
                    empty_conn->rbufp = empty_conn->rbuf;
                    empty_conn->rbuflen = 0;
                    // Save 1 character for NUL;
                    empty_conn->rbufrem = (sizeof empty_conn->rbuf) - 1;
                    empty_conn->sbufp = empty_conn->sbuf;
                    empty_conn->sbuflen = 0;
                    empty_conn->sbufrem = sizeof empty_conn->sbuf;
                    empty_conn = NULL;
                }
            } else {
                puts("not accepting new connection");
            }
        }

        for (size_t i = 0; i < CONNS_MAX;
             fd_max = MAX(fd_max, conns[i].fd), ++i)
        {
            conn_t *conn = &conns[i];

            if (conn->fd == -1) {
                empty_conn = conn;
                continue;
            }

            if (FD_ISSET(conn->fd, &rfds)) {
                ret = conn_recv(conn);
                if (ret == -1) {
                    // Socket is closed.
                    /* printf("closing connection: %d\n", conn->fd); */
                    FD_CLR(conn->fd, &active_rfds);
                    FD_CLR(conn->fd, &active_wfds);
                    conn->fd = -1;
                    continue;
                } else {
                    /* printf("received %d from %d\n", ret, conn->fd); */
                    if (conn->sbuflen) {
                        FD_CLR(conn->fd, &active_rfds);
                        FD_SET(conn->fd, &active_wfds);
                    }
                }
            }

            if (FD_ISSET(conn->fd, &wfds)) {
                ssize_t sent = send(conn->fd, conn->sbuf, conn->sbuflen, 0);
                if (sent < 0) {
                    PERROR();
                } else {
                    /* printf("sent %zd to %d\n", sent, conn->fd); */

                    memmove(conn->sbuf, conn->sbuf + sent,
                            conn->sbuflen - sent);
                    conn->sbufp -= sent;
                    conn->sbuflen -= sent;
                    conn->sbufrem += sent;

                    if (!conn->sbuflen) {
                        FD_SET(conn->fd, &active_rfds);
                        FD_CLR(conn->fd, &active_wfds);
                    }
                }
            }
        }
    }

    for (uint8_t i = 0; i < CONNS_MAX; ++i) {
        if (conns[i].fd != -1) {
            printf("fd %d is still open\n", conns[i].fd);

            ret = shutdown(conns[i].fd, SHUT_RDWR);
            if (ret == -1) {
                PERROR();
            }

            ret = close(conns[i].fd);
            if (ret == -1) {
                PERROR();
            }
        }
    }

    ret = shutdown(sock, SHUT_RDWR);
    if (ret == -1) {
        PERROR();
    }

    ret = close(sock);
    if (ret == -1) {
        PERROR();
    }

    return 0;
}
