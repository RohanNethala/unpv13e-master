#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <errno.h>

#define MAX_CLIENTS 5      /* accept up to 5 simultaneous clients as required */
#define BUFFER_SIZE 1024
#define PORT 12345

/* Message board implementation moved here (was in message_board.c/h) */
#define MAX_MESSAGES 10
#define MAX_MESSAGE_LEN 1024

static char *board[MAX_MESSAGES];
static int board_head = 0;   /* next write index */
static int board_count = 0;  /* number of stored messages (<= MAX_MESSAGES) */

static void board_clear(void)
{
    for (int i = 0; i < MAX_MESSAGES; i++) {
        free(board[i]);
        board[i] = NULL;
    }
    board_head = 0;
    board_count = 0;
}

int post_message(const char *msg)
{
    if (!msg) return -1;
    size_t len = strnlen(msg, MAX_MESSAGE_LEN);
    char *copy = malloc(len + 1);
    if (!copy) return -1;
    memcpy(copy, msg, len);
    copy[len] = '\0';

    if (board[board_head]) {
        free(board[board_head]);
        board[board_head] = NULL;
    }
    board[board_head] = copy;
    board_head = (board_head + 1) % MAX_MESSAGES;
    if (board_count < MAX_MESSAGES) board_count++;
    return 0;
}

char *get_board(void)
{
    size_t total = 1;
    for (int i = 0; i < board_count; i++) {
        int idx = (board_head - board_count + i + MAX_MESSAGES) % MAX_MESSAGES;
        if (board[idx]) total += strlen(board[idx]) + 1;
    }

    char *res = malloc(total);
    if (!res) return NULL;
    res[0] = '\0';

    for (int i = 0; i < board_count; i++) {
        int idx = (board_head - board_count + i + MAX_MESSAGES) % MAX_MESSAGES;
        if (board[idx]) {
            strcat(res, board[idx]);
        }
        if (i < board_count - 1) strcat(res, "\n");
    }
    return res;
}

/* Minimal command handler (moved here). It writes responses to client_fd.
   This keeps behaviour consistent with previous implementation. */
void handle_command(int client_fd, const char *cmd)
{
    if (!cmd) return;

    while (*cmd == ' ' || *cmd == '\t') cmd++;

    if (strncmp(cmd, "POST ", 5) == 0) {
        const char *msg = cmd + 5;
        size_t mlen = strnlen(msg, MAX_MESSAGE_LEN);
        char tmp[MAX_MESSAGE_LEN + 1];
        if (mlen > MAX_MESSAGE_LEN) mlen = MAX_MESSAGE_LEN;
        memcpy(tmp, msg, mlen);
        tmp[mlen] = '\0';
        while (mlen > 0 && (tmp[mlen-1] == '\n' || tmp[mlen-1] == '\r')) {
            tmp[--mlen] = '\0';
        }

        if (post_message(tmp) == 0) {
            const char ok[] = "OK\n";
            send(client_fd, ok, sizeof(ok)-1, 0);
        } else {
            const char err[] = "ERROR: failed to post message\n";
            send(client_fd, err, sizeof(err)-1, 0);
        }
    } else if (strncmp(cmd, "GET", 3) == 0 && (cmd[3] == '\0' || cmd[3] == '\n' || cmd[3] == '\r' || cmd[3] == ' ')) {
        char *board_str = get_board();
        if (board_str) {
            size_t len = strlen(board_str);
            if (len == 0) {
                const char none[] = "\n";
                send(client_fd, none, sizeof(none)-1, 0);
            } else {
                send(client_fd, board_str, len, 0);
                const char nl[] = "\n";
                send(client_fd, nl, 1, 0);
            }
            free(board_str);
        } else {
            const char err[] = "ERROR: unable to retrieve board\n";
            send(client_fd, err, sizeof(err)-1, 0);
        }
    } else {
        const char err[] = "ERROR: unknown command\n";
        send(client_fd, err, sizeof(err)-1, 0);
    }
}

/* ...existing code... (main below) */
int main() {
    int server_fd, new_socket, max_sd, activity, i;
    int client_sockets[MAX_CLIENTS] = {0};
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];

    fd_set readfds;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    /* Allow reuse to avoid "address already in use" during rapid restarts */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("TCP server listening on port %d\n", PORT);

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        max_sd = server_fd;

        for (i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] > 0) FD_SET(client_sockets[i], &readfds);
            if (client_sockets[i] > max_sd) max_sd = client_sockets[i];
        }

        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if ((activity < 0) && (errno != EINTR)) {
            perror("Select error");
        }

        if (FD_ISSET(server_fd, &readfds)) {
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                perror("Accept error");
                exit(EXIT_FAILURE);
            }

            int free_index = -1;
            for (i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i] == 0) {
                    free_index = i;
                    break;
                }
            }

            if (free_index == -1) {
                printf("Too many clients, rejecting connection.\n");
                close(new_socket);
            } else {
                client_sockets[free_index] = new_socket;
                printf("New client connected.\n");
            }
        }

        for (i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];
            if (sd <= 0) continue;

            if (FD_ISSET(sd, &readfds)) {
                int valread = read(sd, buffer, BUFFER_SIZE);
                if (valread == 0) {
                    getpeername(sd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
                    printf("Client disconnected.\n");
                    close(sd);
                    client_sockets[i] = 0;
                } else {
                    buffer[valread] = '\0';
                    handle_command(sd, buffer);
                }
            }
        }
    }

    return 0;
}