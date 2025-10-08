#include "unp.h"
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct {
int tid; // Thread index (1..T)
int start; // Start of Fibonacci range
int end; // End of Fibonacci range
long sum; // Sum of computed Fibonacci values
char result[256]; // Formatted Fibonacci values
} fib_data_t;

long fib(int n) {
    if (n <= 2) return 1;
    long a = 1, b = 1, c;
    for (int i = 3; i <= n; i++) {
        c = a + b;
        a = b;
        b = c;
    }
    return b;
}

// THis is a function that will be called for each subrange
void *fib_worker(void *argument) {
    fib_data_t *arg_fib = (fib_data_t *) argument;
    bool o = false;
    arg_fib->sum = 0;
    arg_fib->result[0] = '\0';
    if (arg_fib->start < 0) return NULL;
    else if (arg_fib->end < 0) return NULL;
    else if (arg_fib->end < arg_fib->start) return NULL;
    for (int i = arg_fib->start; i <= arg_fib->end; i++) {
        // Edge case for the last number to avoid trailing space
        if (i == arg_fib->end){
            o = true;
        }
        if (o == false){
            long n = fib(i);
            arg_fib->sum = arg_fib->sum + n;
            // We need to add a space if we're not at the last number
            snprintf(arg_fib->result + strlen(arg_fib->result), sizeof(arg_fib->result) - strlen(arg_fib->result), "%ld%s", n, " ");
        } else{
            long n = fib(i);
            arg_fib->sum = arg_fib->sum + n;
            // Otherwise, we don't add a space
            snprintf(arg_fib->result + strlen(arg_fib->result), sizeof(arg_fib->result) - strlen(arg_fib->result), "%ld%s", n, "");
        }
    }
    pthread_exit((void *) arg_fib);
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
    printf("[INFO] new client connected from port %d\n", (arg->cliaddr).sin_port);
    int n = 0 ; 
    // Here we get the input from the user
    Write(arg->connfd, "Please enter an integer N: \n", 29);
    char buffer[100];
    ssize_t rn = Read(arg->connfd, buffer, sizeof(buffer) - 1);
    if (rn <= 0){
        Close(arg->connfd);
        free(arg);
        return NULL;
    }
    buffer[rn] = '\0';
    int valid = sscanf(buffer, "%d", &n);
    printf("[Client %d] Received N = %d\n", arg->client_id, n);
    // Here we do some error checking on the user's input
    if (valid != 1){
        Write(arg->connfd, "This number is invalid\n", 23);
        Close(arg->connfd);
        free(arg);
        return NULL;
    } else if (n <= 0){
        Write(arg->connfd, "This number is invalid\n", 23);
        Close(arg->connfd);
        free(arg);
        return NULL;
    }
    // Now we make the worker threads to compute the Fibonacci sums
    fib_data_t thread_data[arg->num_of_threads];
    pthread_t threads[arg->num_of_threads];
    int base = n / arg->num_of_threads;
    int rem = n % arg->num_of_threads;
    int cur = 1;
    bool last = false;
    for (int i = 0; i < arg->num_of_threads; i++){
        thread_data[i].tid = i + 1;
        thread_data[i].start = cur;
        if (last == true){
            thread_data[i].end = cur + base - 1;
        } else {
            thread_data[i].end = cur + base - 1 + (rem > 0 ? 1 : 0);
        }
        // Here we handle the last number in all the intervals
        if (thread_data[i].end > n) {
            thread_data[i].end = n;
        }
        cur = thread_data[i].end + 1;
        if (rem > 0) {
            rem = rem - 1;
            if (rem <= 0) {
                last = true;
            }
        }
        // Now we create the thread
        if (pthread_create(&threads[i], NULL, fib_worker, &thread_data[i]) != 0) {
            err_quit("pthread_create error");
        }
    }
    // Now we compute the sum from joining the threads
    long final_sum = 0;
    int total_count = 0;
    char outbuf[2048];
    ssize_t outputlength = 0;
    for (int i = 0; i < arg->num_of_threads; i++) {
        void *ret;
        pthread_join(threads[i], &ret);
        fib_data_t *td = (fib_data_t *) ret;
        final_sum += td->sum;
        int count = 0;
        if (td->end >= td->start){
            count = td->end - td->start + 1;
        }
        // Here we keep track of the total count of Fibonacci numbers computed
        total_count = total_count + count;
        printf("[Client %d][Thread %d] Range [%d-%d]: sum=%ld\n", arg->client_id, td->tid, td->start, td->end, td->sum);
        int written = snprintf(outbuf + outputlength, sizeof(outbuf) - outputlength,
                "T%d: [%d-%d] -> %s\n",
                td->tid, td->start, td->end,
                (td->result[0] ? td->result : ""));
        if (written > 0) {
            outputlength = outputlength + written;
        }
        if (outputlength >= (int)sizeof(outbuf)) {
            break;
        }

    }
    printf("[Client %d] Total sum = %ld\n", arg->client_id, final_sum);
    int w2 = snprintf(outbuf + outputlength, sizeof(outbuf) - outputlength,
                        "Total computed = %d Fibonacci numbers\nSum = %ld\n",
                        total_count, final_sum);
    if (w2 > 0) outputlength += w2;
    Writen(arg->connfd, outbuf, outputlength);
    Close(arg->connfd);
    free(arg);
    return NULL;
}

int
main(int argc, char **argv)
{
    if (argc != 3) err_quit("usage: ./lab4 port# #ofthreads");
	int					listening_fd, connection_fd;
	socklen_t			clilen;
	struct sockaddr_in	cliaddr, servaddr;

	listening_fd = Socket(AF_INET, SOCK_STREAM, 0);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port        = htons(atoi(argv[1]));
    int num_clients = 0;

	Bind(listening_fd, (SA *) &servaddr, sizeof(servaddr));

	Listen(listening_fd, LISTENQ);
    printf("[INFO] Server listening on port %d\n", atoi(argv[1]));

	for ( ; ; ) {
		clilen = sizeof(cliaddr);
		connection_fd = Accept(listening_fd, (SA *) &cliaddr, &clilen);
        num_clients = num_clients + 1;
        conn_arg_t *connection_arg = malloc(sizeof(conn_arg_t));
        if (connection_arg == NULL) {
            err_quit("malloc error");
            Close(connection_fd);
            continue;
        }
        connection_arg->num_of_threads = atoi(argv[2]);
        connection_arg->connfd = connection_fd;
        connection_arg->cliaddr = cliaddr;
        connection_arg->client_id = num_clients;
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, client_thread, connection_arg) != 0) {
            Close(connection_fd);
            free(connection_arg);
            continue;
        }
        pthread_detach(thread_id);
	}
    return 0;
}
