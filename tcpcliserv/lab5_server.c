/*
 * Lab 5 Echo Server (Instructor-Provided)
 * ---------------------------------------
 * - Listens on 127.0.0.1:9000
 * - Echoes back all received data
 * - Adds small per-message delay (~1.5 ms)
 * - Closes connection after 4 s of inactivity
 *
 * Build: gcc -Wall -O2 -o lab5_server lab5_server.c
 * Run:   ./lab5_server
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 9000
#define BACKLOG     16
#define BUF_SIZE    4096
#define PROCESS_DELAY_US 1500

int main(void) {
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) { perror("socket"); exit(1); }

    int yes = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    if (bind(listenfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listenfd);
        return 1;
    }
    if (listen(listenfd, BACKLOG) < 0) {
        perror("listen");
        close(listenfd);
        return 1;
    }

    printf("[Server] Listening on %s:%d\n", SERVER_IP, SERVER_PORT);

    struct sockaddr_in cli;
    socklen_t clilen = sizeof(cli);
    int connfd = accept(listenfd, (struct sockaddr*)&cli, &clilen);
    if (connfd < 0) { perror("accept"); close(listenfd); return 1; }

    uint8_t buf[BUF_SIZE];
    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(connfd, &rfds);
        struct timeval tv = {.tv_sec = 4, .tv_usec = 0};
        int sel = select(connfd + 1, &rfds, NULL, NULL, &tv);
        if (sel == 0) break; /* idle timeout */

        ssize_t n = recv(connfd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        usleep(PROCESS_DELAY_US);
        send(connfd, buf, n, 0);
    }

    printf("[Server] Idle timeout, closing connection.\n");
    close(connfd);
    close(listenfd);
    return 0;
}