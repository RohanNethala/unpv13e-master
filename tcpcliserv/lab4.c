#include "unp.h"

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
int
main(int argc, char **argv)
{
    if (argc != 3) err_quit("usage: ./lab4 port# #ofthreads");
	int					listenfd, connfd;
	pid_t				childpid;
	socklen_t			clilen;
	struct sockaddr_in	cliaddr, servaddr;

	listenfd = Socket(AF_INET, SOCK_STREAM, 0);
    int num_of_threads = atoi(argv[2]);

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port        = htons(atoi(argv[1]));

	Bind(listenfd, (SA *) &servaddr, sizeof(servaddr));

	Listen(listenfd, LISTENQ);

	for ( ; ; ) {
		clilen = sizeof(cliaddr);
		connfd = Accept(listenfd, (SA *) &cliaddr, &clilen);

		// if ( (childtid = pthread_create()) == 0) {	/* child thread for client */
		// 	Close(listenfd);	/* close listening socket */
		// 	str_echo(connfd);	/* process the request */
		// 	exit(0);
		// }

		if ( (childpid = Fork()) == 0) {	/* child process */
            int n= 0 ;
            int j = scanf("Enter N: %d\n", &n);
            j = j + 1;
            // int i = 0;
            int num_parallel_processes = n / num_of_threads;
            int sums[num_of_threads];
            int index = 0;
            int count = 1;
            for (int i = 0; i < num_of_threads; i++){
                int sum = 0;
                for (int u = 0; u < num_parallel_processes; u++){
                    sum = sum + fib(count);
                    count = count + 1;
                }
                sums[index] = sum;
                index = index + 1;
                // int fib_result = fib(count);
                // fibs[count-1] = fib_result;
                // count = count + 1;
                // met = met + 1;
                // if (met == num_parallel_processes){
                //     met = 1;
                //     int sum1 = 0;
                //     for (int j = 0; j < num_parallel_processes; j++){
                //         sum1 = sum1 + fibs[j];
                //     }
                //     sums[index] = sum1;
                //     index = index + 1;
                // }
            }
            // i = 0;
            int final_sum = 0;
            for (int i = 0; i < num_of_threads;i++){
                final_sum = final_sum + sums[i];
            }
            printf("Final Sum: %d\n", final_sum);
			Close(listenfd);	/* close listening socket */
			str_echo(connfd);	/* process the request */
			exit(0);
		}
        // if (connfd > 0){
        //     int n;
        //     scanf("Enter N: %d\n", &n);
        //     int i = 0;
        //     int num_parallel_processes = n / num_of_threads;
        //     int sums[num_of_threads];
        //     int index = 0;
        //     int count = 1;
        //     for (i; i < num_of_threads; i++){
        //         int sum = 0;
        //         for (int u = 0; u < num_parallel_processes; u++){
        //             sum = sum + fib(count);
        //             count = count + 1;
        //         }
        //         sums[index] = sum;
        //         index = index + 1;
        //         // int fib_result = fib(count);
        //         // fibs[count-1] = fib_result;
        //         // count = count + 1;
        //         // met = met + 1;
        //         // if (met == num_parallel_processes){
        //         //     met = 1;
        //         //     int sum1 = 0;
        //         //     for (int j = 0; j < num_parallel_processes; j++){
        //         //         sum1 = sum1 + fibs[j];
        //         //     }
        //         //     sums[index] = sum1;
        //         //     index = index + 1;
        //         // }
        //     }
        //     i = 0;
        //     int final_sum = 0;
        //     for (i; i < num_of_threads;i++){
        //         final_sum = final_sum + sums[i];
        //     }
        //     printf("Final Sum: %d\n", final_sum);

        // }
		Close(connfd);			/* parent closes connected socket */
	}
}
