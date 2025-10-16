/*
 * chatroom_server.c
 * select() + pthread worker pool chat server
 * - username handshake on connect
 * - join/leave notifications
 * - /me action messages
 * - worker threads format and broadcast messages
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
#include <sys/time.h>
#include <stdbool.h>

#define MAX_NAME     32
#define MAX_MSG      1024
#define MAX_CLIENTS  128
#define INBUF        2048
#define WORKER_MAX   64
#define LISTENQ      128
#define SA struct sockaddr

pthread_mutex_t clients_mtx = PTHREAD_MUTEX_INITIALIZER;

static int client_fds[MAX_CLIENTS];
static char client_names[MAX_CLIENTS][MAX_NAME];
static int max_clients_global = MAX_CLIENTS;

typedef struct Job {
    int sender_fd;
    char username[MAX_NAME];
    char msg[MAX_MSG];
    struct Job *next;
} Job;

typedef struct Queue {
    Job *head;
    Job *tail;
    pthread_mutex_t mtx;
    pthread_cond_t cv;
    bool closed;
} Queue;

static Queue job_queue;
static pthread_t *workers = NULL;
static int worker_count = 4;
static volatile bool running = true;

static void q_init(Queue *q) {
    q->head = q->tail = NULL;
    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init(&q->cv, NULL);
    q->closed = false;
}
static void q_close(Queue *q) {
    pthread_mutex_lock(&q->mtx);
    q->closed = true;
    pthread_cond_broadcast(&q->cv);
    pthread_mutex_unlock(&q->mtx);
}
static void free_job(Job *j) { free(j); }
static void q_push(Queue *q, Job *j) {
    pthread_mutex_lock(&q->mtx);
    if (!q->closed) {
        j->next = NULL;
        if (q->tail == NULL) q->head = q->tail = j;
        else { q->tail->next = j; q->tail = j; }
        pthread_cond_signal(&q->cv);
        pthread_mutex_unlock(&q->mtx);
        return;
    }
    pthread_mutex_unlock(&q->mtx);
    free_job(j);
}
static Job *q_pop(Queue *q) {
    pthread_mutex_lock(&q->mtx);
    while (!q->closed && q->head == NULL) pthread_cond_wait(&q->cv, &q->mtx);
    if (q->head == NULL && q->closed) { pthread_mutex_unlock(&q->mtx); return NULL; }
    Job *j = q->head; q->head = j->next; if (q->head == NULL) q->tail = NULL;
    pthread_mutex_unlock(&q->mtx);
    return j;
}

static void broadcast_to_all(const char *msg, size_t len, int exclude_fd) {
    pthread_mutex_lock(&clients_mtx);
    for (int i = 0; i < max_clients_global; ++i) {
        int fd = client_fds[i];
        if (fd >= 0 && fd != exclude_fd) {
            ssize_t n = send(fd, msg, len, 0);
            (void)n; /* ignore partial write here for simplicity */
        }
    }
    pthread_mutex_unlock(&clients_mtx);
}

static void *worker_thread(void *arg) {
    (void)arg;
    while (running) {
        Job *j = q_pop(&job_queue);
        if (j == NULL) break;

        char out[MAX_NAME + MAX_MSG + 16];
        if (strncmp(j->msg, "/me ", 4) == 0) {
            int r = snprintf(out, sizeof(out), "*%s %s", j->username, j->msg + 4);
            if (r < 0) r = 0; size_t outlen = (size_t)r;
            if (outlen == 0 || out[outlen-1] != '\n') {
                if (outlen + 1 < sizeof(out)) { out[outlen++] = '\n'; out[outlen] = '\0'; }
            }
            broadcast_to_all(out, strlen(out), j->sender_fd);
        } else {
            int r = snprintf(out, sizeof(out), "%s: %s", j->username, j->msg);
            if (r < 0) r = 0; size_t outlen = (size_t)r;
            if (outlen == 0 || out[outlen-1] != '\n') {
                if (outlen + 1 < sizeof(out)) { out[outlen++] = '\n'; out[outlen] = '\0'; }
            }
            broadcast_to_all(out, strlen(out), j->sender_fd);
        }

        free_job(j);
    }
    return NULL;
}

static void trim_newline(char *s) {
    char *p = s + strlen(s) - 1;
    while (p >= s && (*p == '\n' || *p == '\r')) { *p = '\0'; --p; }
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 4) {
        fprintf(stderr, "usage: %s [port] [num_workers] [max_clients]\n", argv[0]);
        fprintf(stderr, "Example: %s 4000 4 64\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    if (argc >= 3) worker_count = atoi(argv[2]);
    if (worker_count <= 0) worker_count = 4;
    if (argc == 4) max_clients_global = atoi(argv[3]);
    if (max_clients_global <= 0 || max_clients_global > MAX_CLIENTS) max_clients_global = MAX_CLIENTS;

    for (int i = 0; i < max_clients_global; ++i) { client_fds[i] = -1; client_names[i][0] = '\0'; }

    q_init(&job_queue);
    workers = calloc(worker_count, sizeof(pthread_t));
    if (!workers) { perror("calloc workers"); exit(EXIT_FAILURE); }
    for (int i = 0; i < worker_count; ++i) pthread_create(&workers[i], NULL, worker_thread, NULL);

    int listening_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listening_fd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    int opt = 1; setsockopt(listening_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if (bind(listening_fd, (SA *)&servaddr, sizeof(servaddr)) < 0) { perror("bind"); exit(EXIT_FAILURE); }
    if (listen(listening_fd, LISTENQ) < 0) { perror("listen"); exit(EXIT_FAILURE); }
    printf("[INFO] Server listening on port %d (workers=%d max_clients=%d)\n", port, worker_count, max_clients_global);

    fd_set master_set, read_fds;
    FD_ZERO(&master_set);
    FD_SET(listening_fd, &master_set);
    int fdmax = listening_fd;

    while (running) {
        read_fds = master_set;
        int rv = select(fdmax + 1, &read_fds, NULL, NULL, NULL);
        if (rv < 0) { if (errno == EINTR) continue; perror("select"); break; }

        if (FD_ISSET(listening_fd, &read_fds)) {
            struct sockaddr_in cliaddr; socklen_t clilen = sizeof(cliaddr);
            int conn = accept(listening_fd, (SA *)&cliaddr, &clilen);
            if (conn < 0) { perror("accept"); }
            else {
                fprintf(stderr, "[INFO] accept conn=%d\n", conn); fflush(stderr);
                pthread_mutex_lock(&clients_mtx);
                int slot = -1;
                for (int i = 0; i < max_clients_global; ++i) if (client_fds[i] < 0) { slot = i; break; }
                if (slot == -1) {
                    const char *msg = "Server full. Try later.\n"; send(conn, msg, strlen(msg), 0); close(conn);
                } else {
                    client_fds[slot] = conn; client_names[slot][0] = '\0'; FD_SET(conn, &master_set);
                    if (conn > fdmax) fdmax = conn;
                    const char *welcome = "Welcome to Chatroom! Please enter your username:\n";
                    send(conn, welcome, strlen(welcome), 0);
                    fprintf(stderr, "[INFO] new client slot=%d fd=%d\n", slot, conn); fflush(stderr);
                }
                pthread_mutex_unlock(&clients_mtx);
            }
        }

        /* Iterate clients without holding clients_mtx across blocking recv.
         * For each client: snapshot fd and name under lock, then perform recv
         * unlocked. When modifying shared state (closing socket, setting name)
         * re-acquire the lock.
         */
        for (int i = 0; i < max_clients_global; ++i) {
            int fd;
            char name_snapshot[MAX_NAME];

            pthread_mutex_lock(&clients_mtx);
            fd = client_fds[i];
            if (fd >= 0) strncpy(name_snapshot, client_names[i], MAX_NAME);
            else name_snapshot[0] = '\0';
            pthread_mutex_unlock(&clients_mtx);

            if (fd < 0) continue;
            if (!FD_ISSET(fd, &read_fds)) continue;

            char buf[INBUF]; ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
            if (n <= 0) {
                fprintf(stderr, "[INFO] recv fd=%d n=%zd\n", fd, n); fflush(stderr);
                /* Need to update shared state under lock */
                pthread_mutex_lock(&clients_mtx);
                if (n == 0) {
                    if (client_names[i][0] != '\0') {
                        char notify[MAX_NAME + 32]; int rn = snprintf(notify, sizeof(notify), "%s has left the chat.\n", client_names[i]);
                        if (rn > 0) broadcast_to_all(notify, (size_t)rn, fd);
                    }
                } else {
                    perror("recv");
                }
                close(fd); FD_CLR(fd, &master_set); client_fds[i] = -1; client_names[i][0] = '\0';
                fprintf(stderr, "[INFO] closed fd=%d slot=%d\n", fd, i); fflush(stderr);
                pthread_mutex_unlock(&clients_mtx);
                continue;
            }
            buf[n] = '\0';

            /* If username not set, treat first line as username. Otherwise push job. */
            pthread_mutex_lock(&clients_mtx);
            bool have_name = (client_names[i][0] != '\0');
            pthread_mutex_unlock(&clients_mtx);

            if (!have_name) {
                trim_newline(buf);
                pthread_mutex_lock(&clients_mtx);
                strncpy(client_names[i], buf, MAX_NAME - 1); client_names[i][MAX_NAME - 1] = '\0';
                pthread_mutex_unlock(&clients_mtx);

                char greet[128]; int glen = snprintf(greet, sizeof(greet), "Let's start chatting %s!\n", buf);
                send(fd, greet, (size_t)glen, 0);
                char notify[MAX_NAME + 32]; int rn = snprintf(notify, sizeof(notify), "%s has joined the chat.\n", buf);
                if (rn > 0) broadcast_to_all(notify, (size_t)rn, fd);
                fprintf(stderr, "[INFO] set name for slot=%d fd=%d name=%s\n", i, fd, buf); fflush(stderr);
            } else {
                trim_newline(buf);
                Job *job = malloc(sizeof(Job)); if (!job) { perror("malloc job"); continue; }
                job->sender_fd = fd;
                pthread_mutex_lock(&clients_mtx);
                strncpy(job->username, client_names[i], MAX_NAME-1); job->username[MAX_NAME-1] = '\0';
                pthread_mutex_unlock(&clients_mtx);
                strncpy(job->msg, buf, MAX_MSG-1); job->msg[MAX_MSG-1] = '\0'; job->next = NULL;
                fprintf(stderr, "[DEBUG] enqueue from slot=%d fd=%d user=%s msg=%s\n", i, fd, job->username, job->msg); fflush(stderr);
                q_push(&job_queue, job);
            }
        }
    }

    q_close(&job_queue);
    for (int i = 0; i < worker_count; ++i) pthread_join(workers[i], NULL);
    free(workers);
    close(listening_fd);
    pthread_mutex_destroy(&clients_mtx);
    return 0;
}
