// ...existing code...
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>

#define MAXLINE 2048
#define SHUTDOWN_TOKEN "SERVER_SHUTDOWN\n"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    const char *server_ip = "127.0.0.1";
    int port = atoi(argv[1]);
    int sockfd;
    struct sockaddr_in servaddr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &servaddr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(1);
    }

    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect");
        exit(1);
    }

    fd_set rset;
    char sendbuf[MAXLINE], recvbuf[MAXLINE];

    FD_ZERO(&rset);

    for (;;) {
        FD_SET(STDIN_FILENO, &rset);
        FD_SET(sockfd, &rset);
        int maxfd = sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO;

        int nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (nready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (FD_ISSET(sockfd, &rset)) {
            ssize_t n = recv(sockfd, recvbuf, sizeof(recvbuf)-1, 0);
            if (n <= 0) {
                /* server closed connection or error */
                printf("Server closed connection\n");
                close(sockfd);
                exit(0);
            }
            recvbuf[n] = '\0';

            /* If server sent shutdown token, print message and exit */
            if (strcmp(recvbuf, SHUTDOWN_TOKEN) == 0 ||
                (n >= (ssize_t)strlen(SHUTDOWN_TOKEN) && strncmp(recvbuf, SHUTDOWN_TOKEN, strlen(SHUTDOWN_TOKEN)) == 0)) {
                printf("Server closed connection\n");
                close(sockfd);
                exit(0);
            }

            /* Otherwise print whatever server sent (GET response or broadcasted posts) */
            fputs(recvbuf, stdout);
            fflush(stdout);
        }

        if (FD_ISSET(STDIN_FILENO, &rset)) {
            if (fgets(sendbuf, sizeof(sendbuf), stdin) == NULL) {
                /* EOF on stdin: close and exit */
                close(sockfd);
                exit(0);
            }
            /* send user input to server */
            size_t len = strlen(sendbuf);
            if (len > 0) {
                ssize_t s = send(sockfd, sendbuf, len, 0);
                (void)s;
            }
        }
    }

    close(sockfd);
    return 0;
}
