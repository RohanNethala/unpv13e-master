#include "unp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <stdbool.h>

#define MAX_NAME     32
#define MAX_MSG      1024
#define MAX_CLIENTS  64
#define INBUF        2048
typedef struct Job {
    int sender_fd;                  // The file descriptor (socket) of the client who sent the message
    char username[MAX_NAME];        // Username of the sender
    char msg[MAX_MSG];              // Raw message text sent by the client
    struct Job *next;               // Pointer to the next Job in the queue (linked-list structure)
} Job;

/*
 * Thread-safe FIFO queue structure.
 * Used for both job_queue (raw messages from clients)
 * and bcast_queue (formatted messages ready to broadcast).
 */
typedef struct Queue {
    Job *head;              // Pointer to the first Job in the queue
    Job *tail;              // Pointer to the last Job in the queue
    pthread_mutex_t mtx;    // Mutex to protect access to the queue
    pthread_cond_t cv;      // Condition variable for thread signaling
    int closed;             // Flag: 1 when queue is closed (no new Jobs)
} Queue;

static Queue job_queue, bcast_queue;

/* ---------------- Queue Utilities ---------------- */
static void q_init(Queue *q) {
    //todo
}
static void q_close(Queue *q) {
    //todo
}
static void q_push(Queue *q, Job *j) {
    //todo
}
static Job *q_pop(Queue *q) {
    //todo
}

/*
 * CSCI 4220 - Assignment 2 Reference Solution
 * Concurrent Chatroom Server (select() + pthread worker pool)
 * Classic IRC-style "/me" action messages: *username text*
/* ---------------- Main ---------------- */
int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: %s [port] [max_clients] (optional num_workers ignored)\n", argv[0]);
        fprintf(stderr, "Example: %s 4000 16\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    int max_clients = (argc == 4) ? atoi(argv[3]) : atoi(argv[2]);

    int listening_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listening_fd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if (bind(listening_fd, (SA *)&servaddr, sizeof(servaddr)) < 0) { perror("bind"); exit(EXIT_FAILURE); }
    if (listen(listening_fd, LISTENQ) < 0) { perror("listen"); exit(EXIT_FAILURE); }
    printf("[INFO] Server listening on port %d\n", port);

    int *clients = calloc(max_clients, sizeof(int));
    char (*client_names)[MAX_NAME] = calloc(max_clients, MAX_NAME);
    if (!clients || !client_names) { perror("calloc"); exit(EXIT_FAILURE); }
    for (int i = 0; i < max_clients; ++i) clients[i] = -1;

    fd_set master_set, read_fds;
    FD_ZERO(&master_set);
    FD_SET(listening_fd, &master_set);
    int fdmax = listening_fd;

    while (1) {
        read_fds = master_set;
        select(fdmax + 1, &read_fds, NULL, NULL, NULL) ;

        if (FD_ISSET(listening_fd, &read_fds)) {
            struct sockaddr_in cliaddr;
            socklen_t clilen = sizeof(cliaddr);
            int conn = accept(listening_fd, (SA *)&cliaddr, &clilen);
            bool found = false;
            int slot = -1;
            for (int i = 0; i < max_clients; ++i) if (clients[i] < 0) { slot = i; found=true; break; }
            if (found == false) {
                send(conn, "Server full. Try later.\n", 22, 0);
                close(conn);
            } else {
                clients[slot] = conn;
                client_names[slot][0] = '\0';
                FD_SET(conn, &master_set);
                if (conn > fdmax) {
                    fdmax = conn;
                }
                send(conn, "Welcome to Chatroom! Please enter your username:\n", 54, 0);
            }
        }
        for (int i = 0; i < max_clients; ++i) {
            int fd = clients[i];
            if (fd < 0) continue;
            if (!FD_ISSET(fd, &read_fds)) continue;
            for ( ; ; ){
                char buf[INBUF];
                ssize_t n = recv(fd, buf, sizeof(buf) - 1,0);
                if (n <= 0) break;
                buf[n] = '\0';
                if (client_names[i][0] == '\0') {
                    char *p = buf; while (*p && *p != '\r' && *p != '\n') ++p; *p = '\0';
                    strncpy(client_names[i], buf, MAX_NAME - 1);
                    client_names[i][MAX_NAME - 1] = '\0';
                    char greet[128]; int glen = snprintf(greet, sizeof(greet), "Let's start chatting %s!\n", client_names[i]);
                    if (glen < 0) glen = 0; if (glen >= (int)sizeof(greet)) glen = (int)sizeof(greet)-1;
                    send(fd, greet, glen, 0);
                    break;
                } else {
                    char out[MAX_MSG + MAX_NAME + 8];
                    int outlen = snprintf(out, sizeof(out), "%s: %s", client_names[i], buf);
                    Job *j = malloc(sizeof(Job));
                    if (outlen < 0) outlen = 0; if (outlen >= (int)sizeof(out)) outlen = (int)sizeof(out)-1;
                    for (int j = 0; j < max_clients; ++j) {
                        if (clients[j] >= 0) send(clients[j], out, outlen, 0);
                    }
                }
            }
            // char buf[INBUF];
            // ssize_t n = recv(fd, buf, sizeof(buf) - 1,0);
            // if (n <= 0) {
            //     close(fd);
            //     FD_CLR(fd, &master_set);
            //     clients[i] = -1;
            //     client_names[i][0] = '\0';
            // } else {
            //     buf[n] = '\0';
            //     if (client_names[i][0] == '\0') {
            //         char *p = buf; while (*p && *p != '\r' && *p != '\n') ++p; *p = '\0';
            //         strncpy(client_names[i], buf, MAX_NAME - 1);
            //         client_names[i][MAX_NAME - 1] = '\0';
            //         char greet[128]; int glen = snprintf(greet, sizeof(greet), "Let's start chatting %s!\n", client_names[i]);
            //         if (glen < 0) glen = 0; if (glen >= (int)sizeof(greet)) glen = (int)sizeof(greet)-1;
            //         send(fd, greet, glen, 0);
            //     } else {
            //         char out[MAX_MSG + MAX_NAME + 8];
            //         int outlen = snprintf(out, sizeof(out), "%s: %s", client_names[i], buf);
            //         if (outlen < 0) outlen = 0; if (outlen >= (int)sizeof(out)) outlen = (int)sizeof(out)-1;
            //         for (int j = 0; j < max_clients; ++j) {
            //             if (clients[j] >= 0) send(clients[j], out, outlen, 0);
            //         }
            //     }
            // }
        }
    }
    return 0;
}
                // if (fd == listening_fd){
                //     // Here we get the input from the user
                //     clilen = sizeof(cliaddr);
                //     connection_fd = accept(listening_fd, (SA *) &cliaddr, &clilen);
                //     FD_SET(connection_fd, &master_set);
                //     if (connection_fd > fdmax) {
                //         fdmax = connection_fd;
                //     }
                //     num_clients = num_clients + 1;
                //     conn_arg_t *connection_arg = malloc(sizeof(conn_arg_t));
                //     if (connection_arg == NULL) {
                //         perror("malloc error");
                //         close(connection_fd);
                //         continue;
                //     }
                //     // Here we set up all the client thread's arguments
                //     connection_arg->num_of_threads = atoi(argv[2]);
                //     connection_arg->connfd = connection_fd;
                //     connection_arg->cliaddr = cliaddr;
                //     connection_arg->client_id = num_clients;
                //     pthread_t thread_id;
                //     // Now we make the client thread
                //     if (pthread_create(&thread_id, NULL, client_thread, connection_arg) != 0) {
                //         close(connection_fd);
                //         free(connection_arg);
                //         continue;
                //     }
                //     pthread_detach(thread_id);                
                // } else {
                //     printf("Penis");
                // }
        // // Here we start accepting new connections from clients
		// clilen = sizeof(cliaddr);
		// connection_fd = accept(listening_fd, (SA *) &cliaddr, &clilen);
        // num_clients = num_clients + 1;
        // conn_arg_t *connection_arg = malloc(sizeof(conn_arg_t));
        // if (connection_arg == NULL) {
        //     perror("malloc error");
        //     close(connection_fd);
        //     continue;
        // }
        // // Here we set up all the client thread's arguments
        // connection_arg->num_of_threads = atoi(argv[2]);
        // connection_arg->connfd = connection_fd;
        // connection_arg->cliaddr = cliaddr;
        // connection_arg->client_id = num_clients;
        // pthread_t thread_id;
        // // Now we make the client thread
        // if (pthread_create(&thread_id, NULL, client_thread, connection_arg) != 0) {
        //     close(connection_fd);
        //     free(connection_arg);
        //     continue;
        // }
        // pthread_detach(thread_id);
        // conn_arg_t *connection_arg = malloc(sizeof(conn_arg_t));
        // if (connection_arg == NULL) {
        //     err_quit("malloc error");
        //     Close(connection_fd);
        //     continue;
        // }
        // Now we make the client thread
        // if (pthread_create(&thread_id, NULL, client_thread, connection_arg) != 0) {
        //     Close(connection_fd);
        //     free(connection_arg);
        //     continue;
        // }
        // pthread_detach(thread_id);
    // return 0;
