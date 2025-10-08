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

#define MAX_NAME     32
#define MAX_MSG      1024
#define MAX_CLIENTS  64
#define INBUF        2048

/*
 * CSCI 4220 - Assignment 2 Reference Solution
 * Concurrent Chatroom Server (select() + pthread worker pool)
 * Classic IRC-style "/me" action messages: *username text*
 *
 * This program demonstrates:
 *   - I/O multiplexing with select()
 *   - Multi-threaded worker pool using pthreads
 *   - Thread-safe producer/consumer queues
 *   - Message broadcasting to multiple clients
 *   - Basic command handling (/who, /me, /quit)
 *
 * Build:
 *   clang -Wall -Wextra -O2 -pthread chatroom_server.c -o chatroom_server.out
 */

/* ---------------- Data Structures ---------------- */

typedef struct Job {
    int sender_fd;                  // The file descriptor (socket) of the client who sent the message
    char username[MAX_NAME];        // Username of the sender
    char msg[MAX_MSG];              // Raw message text sent by the client
    struct Job *next;               // Pointer to the next Job in the queue (linked-list structure)
} Job;

char usernames[MAX_CLIENTS][MAX_NAME];

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

// This struct is for the client thread arguments
typedef struct {
    int num_of_threads;
    int connfd;
    int client_id;
    struct sockaddr_in cliaddr;
} conn_arg_t;

// This function is used for the client threads
void *client_thread(void *varg) {
    conn_arg_t *arg = (conn_arg_t *)varg;
    // printf("[INFO] new client connected from port %d\n", (arg->cliaddr).sin_port);
    write(arg->connfd, "Welcome to Chatroom! Please enter your username:\n", 49);
    char buffer[100];
    char name[MAX_NAME];
    ssize_t rn = read(arg->connfd, buffer, sizeof(buffer) - 1);
    if (rn <= 0){
        close(arg->connfd);
        free(arg);
        return NULL;
    }
    buffer[rn] = '\0';
    int valid = sscanf(buffer, "%s", name);
    /* Build the greeting into a buffer using snprintf, then write the buffer. */
    char greetbuf[128];
    int glen = snprintf(greetbuf, sizeof(greetbuf), "Let's start chatting %s!\n", name);
    if (glen < 0) glen = 0;
    if (glen >= (int)sizeof(greetbuf)) glen = (int)sizeof(greetbuf) - 1; /* snprintf truncation */
    write(arg->connfd, greetbuf, glen);
    close(arg->connfd);
    free(arg);
    return NULL;
}
/* ---------------- Main ---------------- */
int main(int argc, char **argv) {
    int					listening_fd, connection_fd;
	socklen_t			clilen;
	struct sockaddr_in	cliaddr, servaddr;
    //todo
    if (argc != 4) {
        printf("usage: ./chatroom_server.out [port] [num_workers] [max_clients]");
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    int num_workers = atoi(argv[2]);
    int max_clients = atoi(argv[3]);
    char usernames[MAX_CLIENTS][MAX_NAME];
	// int					listening_fd, connection_fd;
	// socklen_t			clilen;
	// struct sockaddr_in	cliaddr, servaddr;

	listening_fd = socket(AF_INET, SOCK_STREAM, 0);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port        = htons(atoi(argv[1]));
    int num_clients = 0;

	bind(listening_fd, (SA *) &servaddr, sizeof(servaddr));

	listen(listening_fd, LISTENQ);
    printf("[INFO] Server listening on port %d\n", atoi(argv[1]));

    for ( ; ; ) {
        // Here we start accepting new connections from clients
		clilen = sizeof(cliaddr);
		connection_fd = accept(listening_fd, (SA *) &cliaddr, &clilen);
        num_clients = num_clients + 1;
        conn_arg_t *connection_arg = malloc(sizeof(conn_arg_t));
        if (connection_arg == NULL) {
            perror("malloc error");
            close(connection_fd);
            continue;
        }
        // Here we set up all the client thread's arguments
        connection_arg->num_of_threads = atoi(argv[2]);
        connection_arg->connfd = connection_fd;
        connection_arg->cliaddr = cliaddr;
        connection_arg->client_id = num_clients;
        pthread_t thread_id;
        // Now we make the client thread
        if (pthread_create(&thread_id, NULL, client_thread, connection_arg) != 0) {
            close(connection_fd);
            free(connection_arg);
            continue;
        }
        pthread_detach(thread_id);
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
	}
    return 0;
}