#include <unp.h>
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

// Here we have some arrays to help us keep track of the clients
int fds_for_each_client[MAX_CLIENTS];
char names_of_the_clients[MAX_CLIENTS][MAX_NAME];
int max_num_clients_global = MAX_CLIENTS;

// Here we have new structs for the job and queue, to help with the threads
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

pthread_mutex_t mtx_for_clients = PTHREAD_MUTEX_INITIALIZER;
Queue job_queue;
Queue bcast_queue;
pthread_t *workers = NULL;
pthread_t broadcaster;
int worker_count;
bool running = true;

// This will initialize a queue, which we use to make the job & broadcast queues
static void q_init(Queue *q) {
    q->head = q->tail = NULL;
    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init(&q->cv, NULL);
    q->closed = false;
}
// This will close the queue and lock and unlock the mutex 
static void q_close(Queue *q) {
    pthread_mutex_lock(&q->mtx);
    q->closed = true;
    pthread_cond_broadcast(&q->cv);
    pthread_mutex_unlock(&q->mtx);
}
// This function will push a new job into the queue
static void q_push(Queue *q, Job *j) {
    pthread_mutex_lock(&q->mtx);
    // Here we see if the queue is closed, and if not, we can place the new job into it
    if (q->closed == false) {
        j->next = NULL;
        if (q->tail == NULL) {
            q->head = q->tail = j;
        }
        else { 
            q->tail->next = j; q->tail = j;
        }
        pthread_cond_signal(&q->cv);
        pthread_mutex_unlock(&q->mtx);
        return;
    }
    pthread_mutex_unlock(&q->mtx);
    free(j);
}

// This function will pop the first job from a queue to be processed
static Job *q_pop(Queue *q) {
    // Here we lock the mutex and pop the first message from the job/broadcast queue
    pthread_mutex_lock(&q->mtx);
    while (!q->closed && q->head == NULL) {
        pthread_cond_wait(&q->cv, &q->mtx);
    }
    if (q->head == NULL && q->closed) { 
        pthread_mutex_unlock(&q->mtx); 
        return NULL;
    }
    // Now here we set the first message in queue to the next one after the first one
    Job *j = q->head; 
    q->head = j->next; 
    if (q->head == NULL){
        q->tail = NULL;
    }
    pthread_mutex_unlock(&q->mtx);
    return j;
}

// Here we make the worker thread that will pop jobs from the queues and broadcast them
static void *worker_thread(void *arg) {
    (void)arg;
    // Here we use a loop to process each job in the queue, until there are none left
    while (running == true) {
        Job *j = q_pop(&job_queue);
        // If there are no jobs left, end the loop
        if (j == NULL) {
            break;
        }
        // Here we obtain the size of the message to display
        char out[MAX_NAME + MAX_MSG + 16];
        int r = -1;
        // Now we see if the user used the /me command, so we know to print the user's name and their msg with *
        if (strncmp(j->msg, "/me ", 4) == 0) {
            char users_name[MAX_NAME];
            strncpy(users_name, j->username, MAX_NAME - 1);
            users_name[MAX_NAME - 1] = '\0';
            char *usr_msg = j->msg + 4;
            r = snprintf(out, sizeof(out), "*%s %s*\n", users_name, usr_msg);
        } else {
            // If they didn't use the /me command, we should just print their name and their message
            char users_name[MAX_NAME];
            strncpy(users_name, j->username, MAX_NAME - 1);
            users_name[MAX_NAME - 1] = '\0';
            char *usr_msg = j->msg;
            r = snprintf(out, sizeof(out), "%s: %s\n", users_name, usr_msg);
        }
        // If the user gave a valid message, we send it to the broadcast queue
        if (r >= 0) {
            Job *b = malloc(sizeof(Job));
            if (b) {
                b->sender_fd = j->sender_fd;
                strncpy(b->msg, out, MAX_MSG - 1);
                b->msg[MAX_MSG - 1] = '\0';
                b->next = NULL;
                q_push(&bcast_queue, b);
            }
        }
        free(j);
    }
    return NULL;
}

// Here is the function for the broadcaster threads
static void *broadcaster_thread(void *arg) {
    (void)arg;
    // Same logic as for the worker thread
    while (running == true) {
        Job *b = q_pop(&bcast_queue);
        if (b == NULL) {
            break;
        }
        size_t len = strnlen(b->msg, MAX_MSG);
        // Now we lock the client mutex and send each message
        pthread_mutex_lock(&mtx_for_clients);
        for (int i = 0; i < max_num_clients_global; ++i) {
            int cfd = fds_for_each_client[i];
            if (cfd >= 0 && cfd != b->sender_fd) {
                send(cfd, b->msg, len, 0);
            }
        }
        pthread_mutex_unlock(&mtx_for_clients);
        free(b);
    }
    return NULL;
}

int main(int argc, char **argv) {
    // Here we have error handling for command line arguments
    if (argc < 2) {
        fprintf(stderr, "usage: %s [port] [num_workers] [max_clients]\n", argv[0]);
        fprintf(stderr, "Example: %s 4000 4 64\n", argv[0]);
        exit(EXIT_FAILURE);
    } else if (argc > 4) {
        fprintf(stderr, "usage: %s [port] [num_workers] [max_clients]\n", argv[0]);
        fprintf(stderr, "Example: %s 4000 4 64\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Here we process the max client and num workers command line arguments
    if (atoi(argv[2]) <= 0){
        worker_count = 4;
    } else {
        worker_count = atoi(argv[2]);
    }
    if (atoi(argv[3]) <= 0){
        max_num_clients_global = MAX_CLIENTS;
    } else if (atoi(argv[3]) > MAX_CLIENTS){
        max_num_clients_global = MAX_CLIENTS;
    } else {
        max_num_clients_global = atoi(argv[3]);
    }

    // Now we set the fds and usernames for each client in max_clients
    for (int i = 0; i < max_num_clients_global; ++i) {
        fds_for_each_client[i] = -1;
        names_of_the_clients[i][0] = '\0';
    }

    // Now we initialize the queues
    q_init(&job_queue);
    q_init(&bcast_queue);
    workers = calloc(worker_count, sizeof(pthread_t));
    if (workers == NULL) {
        perror("error: issue with calloc workers");
        exit(EXIT_FAILURE);
    }
    // Here we create each of the worker threads
    for (int i = 0; i < worker_count; ++i){
        pthread_create(&workers[i], NULL, worker_thread, NULL);
    }
    // Here we make the broadcaster thread that will send the messages
    pthread_create(&broadcaster, NULL, broadcaster_thread, NULL);

    // Now we make the socket, bind to it, and listen and accept connections
    int listening_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listening_fd < 0) {
        perror("error: issue with socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(listening_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(atoi(argv[1]));

    int ret = bind(listening_fd, (SA *)&servaddr, sizeof(servaddr));
    if (ret < 0) {
        perror("error: issue with bind"); exit(EXIT_FAILURE);
    }
    int lis = listen(listening_fd, LISTENQ);
    if (lis < 0) {
        perror("error: issue with listen");
        exit(EXIT_FAILURE);
    }

    fd_set master_set, read_fds;
    FD_ZERO(&master_set);
    FD_SET(listening_fd, &master_set);
    int fdmax = listening_fd;

    // Now we start accepting connections or listen for messages from existing clients
    while (running == true) {
        read_fds = master_set;
        // Here we see if we got a message for an existing client or have a new client connection with select()
        int rv = select(fdmax + 1, &read_fds, NULL, NULL, NULL);
        if (rv < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select"); break;
        }

        // This if part is if we got a new client connection, then we accept it
        if (FD_ISSET(listening_fd, &read_fds)) {
            struct sockaddr_in cliaddr; 
            socklen_t clilen = sizeof(cliaddr);
            int conn = accept(listening_fd, (SA *)&cliaddr, &clilen);
            if (conn < 0) { 
                perror("error: issue with accept");
            }
            else {
                pthread_mutex_lock(&mtx_for_clients);
                int slot = -1;
                for (int i = 0; i < max_num_clients_global; ++i) {
                    if (fds_for_each_client[i] < 0) {
                        slot = i;
                        break;
                    }
                }
                if (slot == -1) {
                    send(conn, "Server full. Try later.\n", 25, 0);
                    close(conn);
                } else {
                    fds_for_each_client[slot] = conn;
                    names_of_the_clients[slot][0] = '\0';
                    FD_SET(conn, &master_set);
                    if (conn > fdmax) {
                        fdmax = conn;
                    }
                    send(conn, "Welcome to Chatroom! Please enter your username:\n", 54, 0);
                }
                pthread_mutex_unlock(&mtx_for_clients);
            }
        }
        for (int i = 0; i < max_num_clients_global; ++i) {
            int fd;
            char name_snapshot[MAX_NAME];

            pthread_mutex_lock(&mtx_for_clients);
            fd = fds_for_each_client[i];
            if (fd >= 0) {
                strncpy(name_snapshot, names_of_the_clients[i], MAX_NAME);
            } else {
                name_snapshot[0] = '\0';
            }
            pthread_mutex_unlock(&mtx_for_clients);

            if (fd < 0) {
                continue;
            }
            if (FD_ISSET(fd, &read_fds) == 0) {
                continue;
            }

            char buf[INBUF];
            ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
            if (n <= 0) {
                /* Need to update shared state under lock */
                pthread_mutex_lock(&mtx_for_clients);
                if (n == 0) {
                    if (names_of_the_clients[i][0] != '\0') {
                        char notify[MAX_NAME + 32];
                        int rn = snprintf(notify, sizeof(notify), "%s has left the chat.\n", names_of_the_clients[i]);
                        if (rn > 0) {
                            Job *b = malloc(sizeof(Job));
                            if (b) {
                                b->sender_fd = fd;
                                strncpy(b->msg, notify, MAX_MSG - 1);
                                b->msg[MAX_MSG - 1] = '\0';
                                b->next = NULL;
                                q_push(&bcast_queue, b);
                            }
                        }
                    }
                } else {
                    perror("recv");
                }
                close(fd);
                FD_CLR(fd, &master_set);
                fds_for_each_client[i] = -1;
                names_of_the_clients[i][0] = '\0';
                pthread_mutex_unlock(&mtx_for_clients);
                continue;
            }
            buf[n] = '\0';

            /* If username not set, treat first line as username. Otherwise push job. */
            pthread_mutex_lock(&mtx_for_clients);
            bool have_name = (names_of_the_clients[i][0] != '\0');
            pthread_mutex_unlock(&mtx_for_clients);

            if (have_name == false) {
                char *p = buf + strlen(buf) - 1;
                while (p >= buf && (*p == '\n' || *p == '\r')) {
                    *p = '\0';
                    p = p - 1;
                }

                // Here we check for duplicate usernames when the user enters in their name
                bool duplicate = false;
                char *duplicate_name = NULL;
                pthread_mutex_lock(&mtx_for_clients);
                for (int k = 0; k < max_num_clients_global; ++k) {
                    if (fds_for_each_client[k] >= 0 && names_of_the_clients[k][0] != '\0') {
                        if (strncasecmp(names_of_the_clients[k], buf, MAX_NAME) == 0) {
                            duplicate_name = names_of_the_clients[k];
                            duplicate = true;
                            break;
                        }
                    }
                }
                if (duplicate) {
                    // If we found a duplicate name, we print out a lowercase version of it in the message
                    char foundname[MAX_NAME];
                    if (duplicate_name) {
                        size_t ln = strnlen(duplicate_name, MAX_NAME - 1);
                        for (size_t ci = 0; ci < ln; ++ci) {
                            foundname[ci] = (char)tolower((unsigned char)duplicate_name[ci]);
                        }
                        foundname[ln] = '\0';
                    } else {
                        size_t ln = strnlen(buf, MAX_NAME - 1);
                        for (size_t ci = 0; ci < ln; ++ci) {
                            foundname[ci] = (char)tolower((unsigned char)buf[ci]);
                        }
                        foundname[ln] = '\0';
                    }
                    pthread_mutex_unlock(&mtx_for_clients);
                    char taken_msg[128];
                    // Here we inform the user that their chosen username is already taken
                    int tn = snprintf(taken_msg, sizeof(taken_msg), "Username \"%s\" is already in use. Try another:\n", foundname);
                    send(fd, taken_msg, (size_t)tn, 0);
                } else {
                    // If there isn't a duplicate username, we accept and set the username
                    strncpy(names_of_the_clients[i], buf, MAX_NAME - 1);
                    names_of_the_clients[i][MAX_NAME - 1] = '\0';
                    pthread_mutex_unlock(&mtx_for_clients);
                    
                    // Now we send the welcome message and broadcast that the user has joined
                    char greet[128];
                    int glen = snprintf(greet, sizeof(greet), "Let's start chatting %s!\n", buf);
                    send(fd, greet, (size_t)glen, 0);
                    char notify[MAX_NAME + 32];
                    int rn = snprintf(notify, sizeof(notify), "%s has joined the chat.\n", buf);
                    if (rn > 0) {
                        Job *b = malloc(sizeof(Job));
                        if (b) {
                            b->sender_fd = fd;
                            strncpy(b->msg, notify, MAX_MSG - 1);
                            b->msg[MAX_MSG - 1] = '\0';
                            b->next = NULL;
                            q_push(&bcast_queue, b);
                        }
                    }
                }
            } else {
                char *p = buf + strlen(buf) - 1;
                while (p >= buf && (*p == '\n' || *p == '\r')) {
                    *p = '\0'; --p;
                }

                /* handle commands locally */
                if (strcmp(buf, "/quit") == 0) {
                    /* close the connection */
                    pthread_mutex_lock(&mtx_for_clients);
                    close(fd);
                    FD_CLR(fd, &master_set);
                    fds_for_each_client[i] = -1;
                    char notify[MAX_NAME + 32];
                    int rn = snprintf(notify, sizeof(notify), "%s has left the chat.\n", names_of_the_clients[i]);
                    names_of_the_clients[i][0] = '\0';
                    pthread_mutex_unlock(&mtx_for_clients);
                    if (rn > 0) {
                        pthread_mutex_lock(&mtx_for_clients);
                        for (int ii = 0; ii < max_num_clients_global; ++ii) {
                            int cfd = fds_for_each_client[ii];
                            if (cfd >= 0 && cfd != fd) send(cfd, notify, (size_t)rn, 0);
                        }
                        pthread_mutex_unlock(&mtx_for_clients);
                    }
                    continue;
                } else if (strcmp(buf, "/who") == 0) {
                    /* send list of users back to this fd */
                    char listbuf[4096];
                    size_t pos = 0;
                    pthread_mutex_lock(&mtx_for_clients);
                    for (int k = 0; k < max_num_clients_global; ++k) {
                        if (fds_for_each_client[k] >= 0 && names_of_the_clients[k][0] != '\0') {
                            int wn = snprintf(listbuf + pos, sizeof(listbuf) - pos, "%s\n", names_of_the_clients[k]);
                            if (wn > 0) {
                                size_t wn_2 = (size_t)wn;
                                pos += wn_2;
                            }
                            if (pos >= sizeof(listbuf) - 64) {
                                break;
                            }
                        }
                    }
                    pthread_mutex_unlock(&mtx_for_clients);
                    if (pos > 0) {
                        send(fd, listbuf, pos, 0);
                    } else {
                        send(fd, "No users online\n", 16, 0);
                    }
                    continue;
                }

                /* not a handled command: enqueue for worker to print */
                Job *job = malloc(sizeof(Job)); 
                if (!job) {
                    perror("malloc job"); 
                    continue;
                }
                job->sender_fd = fd;
                pthread_mutex_lock(&mtx_for_clients);
                strncpy(job->username, names_of_the_clients[i], MAX_NAME-1); 
                job->username[MAX_NAME-1] = '\0';
                pthread_mutex_unlock(&mtx_for_clients);
                strncpy(job->msg, buf, MAX_MSG-1); 
                job->msg[MAX_MSG-1] = '\0'; 
                job->next = NULL;
                q_push(&job_queue, job);
            }
        }
    }

    q_close(&job_queue);
    for (int i = 0; i < worker_count; ++i) {
        pthread_join(workers[i], NULL);
    }
    /* shutdown broadcaster */
    q_close(&bcast_queue);
    pthread_join(broadcaster, NULL);
    free(workers);
    close(listening_fd);
    pthread_mutex_destroy(&mtx_for_clients);
    return 0;
}
