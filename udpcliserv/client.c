#include "unp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void
str_cli_fp(FILE *fp, int sockfd)
{
    int maxfdp1;
    fd_set rset;
    char sendline[MAXLINE], recvline[MAXLINE];

    FD_ZERO(&rset);
    for (;;) {
        FD_SET(fileno(fp), &rset);
        FD_SET(sockfd, &rset);
        maxfdp1 = max(fileno(fp), sockfd) + 1;
        Select(maxfdp1, &rset, NULL, NULL, NULL);

        if (FD_ISSET(sockfd, &rset)) { /* socket is readable */
            int n = Readline(sockfd, recvline, MAXLINE);
            if (n == 0) {
                printf("Server closed connection\n");
                return;
            }
            Fputs(recvline, stdout);
        }

        if (FD_ISSET(fileno(fp), &rset)) { /* input is readable */
            if (Fgets(sendline, MAXLINE, fp) == NULL)
                return; /* EOF on stdin -> exit */
            Writen(sockfd, sendline, strlen(sendline));
        }
    }
}

int
main(int argc, char **argv)
{
    int sockfd, port;
    struct sockaddr_in servaddr;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    port = atoi(argv[1]);

    sockfd = Socket(AF_INET, SOCK_STREAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    Inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr);

    Connect(sockfd, (SA *)&servaddr, sizeof(servaddr));

    printf("Connected to server on port %d\n", port);

    str_cli_fp(stdin, sockfd);

    Close(sockfd);
    return 0;
}
