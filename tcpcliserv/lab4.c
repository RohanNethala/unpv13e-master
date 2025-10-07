#include "unp.h"
#include <pthread.h>

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

void *worker_thread(void *arg) {
    fib_data_t *fib_data = (fib_data_t *)arg;
    fib_data->sum = 0;
    fib_data->result[0] = '\0';
    char temp[32];

    for (int i = fib_data->start; i <= fib_data->end; i++) {
        long temp_fib = fib(i);
        fib_data->sum += temp_fib;
        snprintf(temp, sizeof(temp), "%ld ", temp_fib);
        strncat(fib_data->result, temp);
    }

    printf("[Client %d][Thread %d] Range [%d–%d]: sum=%ld\n", /*something from client*/, fib_data->tid, fib_data->start, fib_data->end, fib_data->sum);

    pthread_exit((void*)fib_data);
}

void *client_handler(void *arg) {
    

}

int main(int argc, char **argv){
    if (argc != 3) err_quit("usage: ./lab4 port# #ofthreads");
	int listening_fd, connection_fd;
    socklen_t clilen;
    struct sockaddr_in cliaddr, servaddr;

	listening_fd = Socket(AF_INET, SOCK_STREAM, 0);
    int num_of_threads = atoi(argv[2]);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port        = htons(atoi(argv[1]));

	Bind(listening_fd, (SA *) &servaddr, sizeof(servaddr));

	Listen(listening_fd, LISTENQ);
    printf("[INFO] Server listening on port %s\n", argv[1]);


	for ( ; ; ) {
		clilen = sizeof(cliaddr);
		connection_fd = Accept(listening_fd, (SA *) &cliaddr, &clilen);
        printf("[INFO] New client from port %d\n", ntohs(cliaddr.sin_port));

        pthread_t client_tid;
        pthread_create(&client_tid, NULL, client_handler, (void *)&connection_fd);
	}
}
