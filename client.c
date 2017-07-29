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
#include <sys/random.h>

#define PERROR()                                        \
    do {                                                \
        char buf[512];                                  \
        snprintf(buf, sizeof buf, "%d", __LINE__);      \
        perror(buf);                                    \
    } while (0);

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

int main(int argc, char **argv) {
    int ret;
    int sock;
    uint8_t rndbuf[1024];
    char buf[1024];
    char buf2[1024];
    int buflen;

    if (argc < 2) {
        printf("give a port number\n");
        return 1;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(atoi(argv[1])),
        .sin_addr = {
            .s_addr = htonl(0x7F000001) // 127.0.0.1
        }
    };

 START:
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        PERROR();
        return 2;
    }

    ret = connect(sock, (struct sockaddr*)&addr, sizeof addr);
    if (ret < 0) {
        PERROR();
        goto CLOSE;
    } else {
        /* puts("connection is open"); */
    }

 GET_RANDOM:
    ret = getrandom(rndbuf, 1, 0);
    if (ret != 1) {
        goto GET_RANDOM;
    }

    if (rndbuf[0] == 0) {
        goto CLOSE;
    }

    ret = getrandom(&rndbuf[1], rndbuf[0], 0);
    if (ret != rndbuf[0]) {
        goto GET_RANDOM;
    }

    buflen = 0;
    for (int i = 1; i <= rndbuf[0]; ++i) {
        buflen += sprintf(buf + buflen, "%02X", rndbuf[i]);
    }
    buf[buflen++] = '\r';
    buf[buflen++] = '\n';

    ssize_t len = send(sock, buf, buflen, 0);
    if (len != buflen) {
        puts("data sent incomplete");
        return 1;
    }
    /* else { */
    /*     printf("=> %.*s\n", buflen - 2, buf); */
    /* } */

    len = recv(sock, buf2, sizeof buf2, 0);
    if (len != buflen) {
        printf("<= %.*s\n", (int)len - 2, buf2);
        puts("bad response 1");
        return 2;
    }

    reverse_string(buf2, len - 2);

    if (memcmp(buf, buf2, buflen) != 0) {
        printf("<= %.*s\n", (int)len - 2, buf2);
        puts("bad response 2");
        return 3;
    } else {
        printf("%.*s", (int)len, buf2);
    }

    goto GET_RANDOM;

 CLOSE:
    shutdown(sock, SHUT_RDWR);
    close(sock);
    /* puts("connection is closed"); */
    usleep(10000);

    goto START;

    return 0;
}
