// ...existing code...
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <errno.h>
#include <signal.h>

#define MAX_CLIENTS 5
#define MAX_MESSAGES 10
#define MAX_MSG_LEN 1024
#define LISTEN_BACKLOG 5
#define SHUTDOWN_TOKEN "SERVER_SHUTDOWN\n"

static int client_sockets[MAX_CLIENTS];
static int server_fd = -1;

/* circular message board */
static char messages[MAX_MESSAGES][MAX_MSG_LEN];
static int msg_head = 0;
static int msg_count = 0;

static void board_clear(void) {
    for (int i = 0; i < MAX_MESSAGES; i++)
        messages[i][0] = '\0';
    msg_head = 0;
    msg_count = 0;
}

static void add_message(const char *msg) {
    strncpy(messages[msg_head], msg, MAX_MSG_LEN-1);
    messages[msg_head][MAX_MSG_LEN-1] = '\0';
    msg_head = (msg_head + 1) % MAX_MESSAGES;
    if (msg_count < MAX_MESSAGES) msg_count++;
}

static void get_messages_string(char *out, size_t outlen) {
    out[0] = '\0';
    if (msg_count == 0) return;
    int start = (msg_head - msg_count + MAX_MESSAGES) % MAX_MESSAGES;
    for (int i = 0; i < msg_count; i++) {
        int idx = (start + i) % MAX_MESSAGES;
        strncat(out, messages[idx], outlen - strlen(out) - 1);
        if (i < msg_count - 1) strncat(out, "\n", outlen - strlen(out) - 1);
    }
}

/* send msg to all clients except exclude_fd (if exclude_fd < 0 send to all) */
static void broadcast_message(int exclude_fd, const char *msg, size_t len) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        int fd = client_sockets[i];
        if (fd > 0 && fd != exclude_fd) {
            ssize_t s = send(fd, msg, len, 0);
            (void)s;
        }
    }
}

/* SIGINT handler: notify clients, close sockets and exit */
static void handle_sigint(int sig) {
    /* print requested message when Ctrl+C (SIGINT) received */
    printf("Shutting down server due to EOF.\n");
    fflush(stdout);

    const char *token = SHUTDOWN_TOKEN;
    /* notify clients */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] > 0) {
            send(client_sockets[i], token, strlen(token), 0);
            shutdown(client_sockets[i], SHUT_RDWR);
            close(client_sockets[i]);
            client_sockets[i] = 0;
        }
    }
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    board_clear();
    _exit(0);
}

int main(int argc, char **argv) {
    struct sockaddr_in servaddr, cliaddr;
    socklen_t clilen;
    fd_set rset, allset;
    int i, maxi = -1, maxfd;
    char buf[MAX_MSG_LEN];

    const int BASE_PORT = 9877;
    int offset = 0;
    int port;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [offset]\n", argv[0]);
        exit(1);
    }
    if (argc == 2) {
        offset = atoi(argv[1]);
    }
    /* if offset is 0 this yields BASE_PORT, otherwise BASE_PORT + offset */
    port = BASE_PORT + offset;

    for (i = 0; i < MAX_CLIENTS; i++) client_sockets[i] = 0;
    board_clear();

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(server_fd, LISTEN_BACKLOG) < 0) {
        perror("listen");
        exit(1);
    }

    /* install SIGINT handler */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);

    FD_ZERO(&allset);
    FD_SET(server_fd, &allset);
    maxfd = server_fd;

    printf("TCP server listening on port %d\n", port);

    for (;;) {
        rset = allset;
        int nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (nready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (FD_ISSET(server_fd, &rset)) {
            clilen = sizeof(cliaddr);
            int connfd = accept(server_fd, (struct sockaddr *)&cliaddr, &clilen);
            if (connfd < 0) {
                if (errno == EINTR) continue;
                perror("accept");
                continue;
            }
            int idx = -1;
            for (i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i] == 0) { idx = i; break; }
            }
            if (idx == -1) {
                const char *msg = "Too many clients\n";
                send(connfd, msg, strlen(msg), 0);
                close(connfd);
            } else {
                client_sockets[idx] = connfd;
                FD_SET(connfd, &allset);
                if (connfd > maxfd) maxfd = connfd;
                if (idx > maxi) maxi = idx;
                printf("New client connected.\n");
            }
            if (--nready <= 0) continue;
        }

        for (i = 0; i <= maxi; i++) {
            int sockfd = client_sockets[i];
            if (sockfd <= 0) continue;
            if (FD_ISSET(sockfd, &rset)) {
                ssize_t n = recv(sockfd, buf, sizeof(buf)-1, 0);
                if (n <= 0) {
                    /* client closed */
                    printf("Client disconnected.\n");
                    close(sockfd);
                    FD_CLR(sockfd, &allset);
                    client_sockets[i] = 0;
                } else {
                    buf[n] = '\0';
                    /* strip trailing CR/LF */
                    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) { buf[n-1] = '\0'; n--; }

                    if (strncmp(buf, "POST ", 5) == 0) {
                        const char *msg = buf + 5;
                        char msg_with_nl[MAX_MSG_LEN];
                        snprintf(msg_with_nl, sizeof(msg_with_nl), "%s\n", msg);
                        add_message(msg_with_nl);
                        broadcast_message(sockfd, msg_with_nl, strlen(msg_with_nl));
                    } else if (strcmp(buf, "GET") == 0) {
                        char out[MAX_MESSAGES * (MAX_MSG_LEN + 1)];
                        get_messages_string(out, sizeof(out));
                        if (strlen(out) == 0) {
                            const char *empty = "\n";
                            send(sockfd, empty, 1, 0);
                        } else {
                            send(sockfd, out, strlen(out), 0);
                            send(sockfd, "\n", 1, 0);
                        }
                    } else {
                        const char *err = "ERROR: unknown command\n";
                        send(sockfd, err, strlen(err), 0);
                    }
                }
                if (--nready <= 0) break;
            }
        }
    }

    /* cleanup */
    handle_sigint(0);
    return 0;
}
// ...existing code...
