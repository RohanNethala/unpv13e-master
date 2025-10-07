#include "unp.h"
#include <stdbool.h>

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
        if (i == arg_fib->end){
            o = true;
        }
        if (o == false){
            arg_fib->sum += fib(i);
            // change this to using a boolean variable for the if condition at the end
            snprintf(arg_fib->result + strlen(arg_fib->result), sizeof(arg_fib->result) - strlen(arg_fib->result), "%ld%s", fib(i), " ");
        } else{
            arg_fib->sum += fib(i);
            snprintf(arg_fib->result + strlen(arg_fib->result), sizeof(arg_fib->result) - strlen(arg_fib->result), "%ld%s", fib(i), "");
            // sprintf(d->result, "%s%d ", d->result, fib(i));
        }
    }
    pthread_exit((void *) arg_fib);
}
int
main(int argc, char **argv)
{
    if (argc != 3) err_quit("usage: ./lab4 port# #ofthreads");
	int					listening_fd, connection_fd;
	pid_t				childpid;
	socklen_t			clilen;
	struct sockaddr_in	cliaddr, servaddr;

	listening_fd = Socket(AF_INET, SOCK_STREAM, 0);
    int num_of_threads = atoi(argv[2]);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port        = htons(atoi(argv[1]));

	Bind(listening_fd, (SA *) &servaddr, sizeof(servaddr));

	Listen(listening_fd, LISTENQ);

	for ( ; ; ) {
		clilen = sizeof(cliaddr);
		connection_fd = Accept(listening_fd, (SA *) &cliaddr, &clilen);
		if ( (childpid = Fork()) == 0) {
            Close(listening_fd);
            int n = 0 ; 
            Write(connection_fd, "Please enter an integer N: \n", 29);
            char buffer[100];
            ssize_t rn = Read(connection_fd, buffer, sizeof(buffer) - 1);
            if (rn <= 0){
                Close(connection_fd);
                exit(0);
            }
            buffer[rn] = '\0';
            int valid = sscanf(buffer, "%d", &n);
            if (valid != 1){
                Write(connection_fd, "This number is invalid\n", 23);
                Close(connection_fd);
                exit(0);
            } else if (n <= 0){
                Write(connection_fd, "This number is invalid\n", 23);
                Close(connection_fd);
                exit(0);
            }
            fib_data_t thread_data[num_of_threads];
            pthread_t threads[num_of_threads];
            int base = n / num_of_threads;
            int rem = n % num_of_threads;
            int cur = 1;
            for (int i = 0; i < num_of_threads; i++){
                thread_data[i].tid = i + 1;
                thread_data[i].start = cur;
                thread_data[i].end = cur + base - 1 + (rem > 0 ? 1 : 0);
                if (thread_data[i].end > n) thread_data[i].end = n;
                cur = thread_data[i].end + 1;
                if (rem > 0) rem--;

                if (pthread_create(&threads[i], NULL, fib_worker, &thread_data[i]) != 0) {
                    err_quit("pthread_create error");
                }
            }
            long final_sum = 0;
            int total_count = 0;
            char outbuf[2048];
            ssize_t outputlength = 0;
            for (int i = 0; i < num_of_threads; i++) {
                void *ret;
                pthread_join(threads[i], &ret);
                fib_data_t *td = (fib_data_t *) ret;
                final_sum += td->sum;
                int count = 0;
                if (td->end >= td->start) count = td->end - td->start + 1;
                total_count += count;
                int written = snprintf(outbuf + outputlength, sizeof(outbuf) - outputlength,
                        "T%d: [%d-%d] -> %s\n",
                        td->tid, td->start, td->end,
                        (td->result[0] ? td->result : ""));
                if (written > 0) outputlength += written;
                if (outputlength >= (int)sizeof(outbuf)) break;

            }
            int w2 = snprintf(outbuf + outputlength, sizeof(outbuf) - outputlength,
                              "Total computed = %d Fibonacci numbers\nSum = %ld\n",
                              total_count, final_sum);
            if (w2 > 0) outputlength += w2;
            Writen(connection_fd, outbuf, outputlength);
            Close(connection_fd);
			exit(0);
		}
		Close(connection_fd);
	}
}
