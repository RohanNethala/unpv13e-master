
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

/* ---------------- Data Structures ---------------- */

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



/* ---------------- Main ---------------- */
int main(int argc, char **argv) {
    //todo
}