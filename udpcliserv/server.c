#include "unp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_CLIENTS 5
#define BOARD_SIZE 10

int
main(int argc, char **argv)
{
    int listenfd, connfd, sockfd;
    int i, maxi, maxfd, nready;
    int clients[MAX_CLIENTS];
    ssize_t n;
    fd_set rset, allset;
    char buf[MAXLINE];
    socklen_t clilen;
    struct sockaddr_in servaddr, cliaddr;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <offset>\n", argv[0]);
        exit(1);
    }

    int offset = atoi(argv[1]);
    int port = 9877 + offset;

    listenfd = Socket(AF_INET, SOCK_STREAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    Bind(listenfd, (SA *)&servaddr, sizeof(servaddr));

    Listen(listenfd, LISTENQ);

    printf("TCP server listening on port %d\n", port);

    /* message board */
    char board[BOARD_SIZE][MAXLINE];
    int board_count = 0; /* number of messages currently stored (<= BOARD_SIZE) */
    int board_next = 0;  /* next index to write (circular buffer) */

    /* initialize client list */
    for (i = 0; i < MAX_CLIENTS; i++)
        clients[i] = -1;
    maxi = -1;

    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    FD_SET(fileno(stdin), &allset);
    maxfd = max(listenfd, fileno(stdin));

    for (;;) {
        rset = allset;
        nready = Select(maxfd + 1, &rset, NULL, NULL, NULL);

        /* check for new connection */
        if (FD_ISSET(listenfd, &rset)) {
            clilen = sizeof(cliaddr);
            connfd = Accept(listenfd, (SA *)&cliaddr, &clilen);

            /* count active clients */
            int active = 0;
            for (i = 0; i < MAX_CLIENTS; i++)
                if (clients[i] >= 0) active++;

            if (active >= MAX_CLIENTS) {
                printf("Too many clients, rejecting connection.\n");
                Close(connfd);
            } else {
                /* add to client list */
                for (i = 0; i < MAX_CLIENTS; i++)
                    if (clients[i] < 0) {
                        clients[i] = connfd;
                        break;
                    }
                if (i > maxi) maxi = i;
                FD_SET(connfd, &allset);
                if (connfd > maxfd) maxfd = connfd;
                printf("New client connected.\n");
            }

            if (--nready <= 0)
                continue;
        }

        /* check for stdin EOF or input */
        if (FD_ISSET(fileno(stdin), &rset)) {
            /* read a line from stdin; on EOF, shutdown */
            if (fgets(buf, sizeof(buf), stdin) == NULL) {
                printf("Shutting down server due to EOF.\n");
                /* close all client sockets */
                for (i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i] >= 0) Close(clients[i]);
                }
                Close(listenfd);
                exit(0);
            }
            /* otherwise ignore stdin input */
            if (--nready <= 0)
                continue;
        }

        /* check all clients for data */
        for (i = 0; i < MAX_CLIENTS; i++) {
            if ((sockfd = clients[i]) < 0)
                continue;
            if (FD_ISSET(sockfd, &rset)) {
                n = Readline(sockfd, buf, MAXLINE);
                if (n == 0) {
                    /* connection closed by client */
                    Close(sockfd);
                    FD_CLR(sockfd, &allset);
                    clients[i] = -1;
                    printf("Client disconnected.\n");
                } else {
                    /* remove trailing newline */
                    if (n > 0 && buf[n-1] == '\n') buf[n-1] = '\0';

                    if (strncmp(buf, "POST ", 5) == 0) {
                        char *msg = buf + 5;
                        /* store message in board */
                        memset(board[board_next], 0, MAXLINE);
                        strncpy(board[board_next], msg, MAXLINE-1);
                        board_next = (board_next + 1) % BOARD_SIZE;
                        if (board_count < BOARD_SIZE) board_count++;

                        /* broadcast to all other clients */
                        int j;
                        char sendline[MAXLINE];
                        snprintf(sendline, sizeof(sendline), "%s\n", msg);
                        for (j = 0; j < MAX_CLIENTS; j++) {
                            int cs = clients[j];
                            if (cs >= 0 && cs != sockfd) {
                                if (Writen(cs, sendline, strlen(sendline)) < 0) {
                                    /* ignore write errors for now */
                                }
                            }
                        }
                    } else if (strcmp(buf, "GET") == 0) {
                        /* send current board contents in order oldest->newest */
                        int k, idx;
                        for (k = 0; k < board_count; k++) {
                            idx = (board_next - board_count + k + BOARD_SIZE) % BOARD_SIZE;
                            char out[MAXLINE];
                            snprintf(out, sizeof(out), "%s\n", board[idx]);
                            Writen(sockfd, out, strlen(out));
                        }
                    } else {
                        /* Unknown command: ignore or optionally send error */
                        /* We'll ignore silently for this lab */
                    }
                }

                if (--nready <= 0)
                    break;
            }
        }
    }

    return 0;
}
