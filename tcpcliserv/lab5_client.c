/*
 * Lab 5 Client (Starter Template): Socket Options & Real-Time IoT “Sensor Client”
 *
 * Start server first:
 *   ./lab5_server &
 * Then run client:
 *   ./lab5_client
 *
 * Your tasks (TODOs):
 *   1. Use getsockopt() to print SO_SNDBUF, SO_RCVBUF, TCP_MAXSEG, TCP_NODELAY, SO_KEEPALIVE
 *   2. Double both buffer sizes and enable TCP_NODELAY using setsockopt()
 *   3. Run latency experiments (send two 1-byte messages back-to-back and measure RTT)
 *   4. Enable SO_KEEPALIVE and wait until the server closes after idle timeout.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 9000
#define ITERATIONS  500

/* ---------- Utility helpers ---------- */

static int send_all(int fd, const void* buf, size_t len) {
    const uint8_t* p = buf;
    size_t left = len;
    while (left > 0) {
        ssize_t n = send(fd, p, left, 0);
        if (n <= 0) return -1;
        left -= (size_t)n;
        p += n;
    }
    return 0;
}

static int recv_all(int fd, void* buf, size_t len) {
    uint8_t* p = buf;
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

/* ---------- TODO 1: Print socket options ---------- */
/* Hint: use getsockopt(level, optname) and printf each value. */
static void print_socket_options(int fd) {
    int val;
    socklen_t len = sizeof(val);
    
    // SO_SNDBUF - Send buffer size
    if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &val, &len) == 0) {
        printf("  SO_SNDBUF = %d bytes\n", val);
    }
    
    // SO_RCVBUF - Receive buffer size
    if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &val, &len) == 0) {
        printf("  SO_RCVBUF = %d bytes\n", val);
    }
    
    // TCP_MAXSEG - Maximum segment size
    if (getsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, &val, &len) == 0) {
        printf("  TCP_MAXSEG = %d bytes\n", val);
    }
    
    // TCP_NODELAY - Disable Nagle's algorithm
    if (getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, &len) == 0) {
        printf("  TCP_NODELAY = %s\n", val ? "ON" : "OFF");
    }
    
    // SO_KEEPALIVE - Keep-alive option
    if (getsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, &len) == 0) {
        printf("  SO_KEEPALIVE = %s\n", val ? "ON" : "OFF");
    }
}

/* ---------- TODO 2: Modify socket options ---------- */
/* Double buffer sizes and enable TCP_NODELAY */
static void modify_socket_options(int fd) {
    int val;
    socklen_t len = sizeof(val);
    
    // Get current send buffer size and double it
    if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &val, &len) == 0) {
        int new_sndbuf = val * 2;
        if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &new_sndbuf, sizeof(new_sndbuf)) == 0) {
            printf("  Doubled SO_SNDBUF from %d to %d bytes\n", val, new_sndbuf);
        }
    }
    
    // Get current receive buffer size and double it
    if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &val, &len) == 0) {
        int new_rcvbuf = val * 2;
        if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &new_rcvbuf, sizeof(new_rcvbuf)) == 0) {
            printf("  Doubled SO_RCVBUF from %d to %d bytes\n", val, new_rcvbuf);
        }
    }
    
    // Enable TCP_NODELAY (disable Nagle's algorithm)
    val = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == 0) {
        printf("  Enabled TCP_NODELAY\n");
    }
}

/* ---------- TODO 3: RTT experiment ---------- */
/* Send 'A' and 'B' back-to-back, receive both echoes, measure round-trip time. */
static double run_rtt_experiment(int fd, int iters) {
    double total_time = 0.0;
    char msg1 = 'A';
    char msg2 = 'B';
    char echo1, echo2;
    
    for (int i = 0; i < iters; i++) {
        struct timespec start, end;
        
        // Start timing
        clock_gettime(CLOCK_MONOTONIC, &start);
        
        // Send two messages back-to-back
        if (send_all(fd, &msg1, 1) != 0 || send_all(fd, &msg2, 1) != 0) {
            fprintf(stderr, "Send failed\n");
            break;
        }
        
        // Receive both echoes
        if (recv_all(fd, &echo1, 1) != 0 || recv_all(fd, &echo2, 1) != 0) {
            fprintf(stderr, "Recv failed\n");
            break;
        }
        
        // Stop timing
        clock_gettime(CLOCK_MONOTONIC, &end);
        
        // Calculate elapsed time in milliseconds
        double elapsed = (end.tv_sec - start.tv_sec) * 1000.0 + 
                        (end.tv_nsec - start.tv_nsec) / 1000000.0;
        total_time += elapsed;
    }
    
    return total_time / iters;  // Return average RTT in milliseconds
}

/* ---------- TODO 4: Enable keepalive ---------- */
/* Enable SO_KEEPALIVE and short timers if supported. */
static void enable_keepalive(int fd) {
    int val;
    
    // Enable SO_KEEPALIVE
    val = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == 0) {
        return;
    }
    
    // Set TCP_KEEPIDLE (time before sending keepalive probes) - 60 seconds
    val = 60;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) == 0) {
        return;
    }
    
    // Set TCP_KEEPINTVL (interval between keepalive probes) - 10 seconds
    val = 10;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val)) == 0) {
        return;
    }
    
    // Set TCP_KEEPCNT (number of keepalive probes before giving up) - 3 probes
    val = 3;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val)) == 0) {
        return;
    }
}

/* ---------- Main ---------- */

int main(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }
    printf("[Client] Connected to %s:%d\n\n", SERVER_IP, SERVER_PORT);

    puts("[Initial Socket Options]");
    print_socket_options(fd);
    puts("");

    puts("[Modify Socket Options]");
    modify_socket_options(fd);
    puts("\n[After Modification]");
    print_socket_options(fd);
    puts("");

    printf("[Experiment] Nagle ON (default)\n");
    double avg_on = run_rtt_experiment(fd, ITERATIONS);
    printf("Average RTT = %.2f ms\n\n", avg_on);

    printf("[Experiment] Nagle OFF (TCP_NODELAY=1)\n");
    double avg_off = run_rtt_experiment(fd, ITERATIONS);
    printf("Average RTT = %.2f ms\n\n", avg_off);

    enable_keepalive(fd);

    uint8_t tmp;
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (1) {
        ssize_t n = recv(fd, &tmp, 1, 0);
        if (n == 0) {
            clock_gettime(CLOCK_MONOTONIC, &end);
            double secs = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec)/1e9;
            printf("[Keepalive] Detected remote close after ~%.2f s\n", secs);
            break;
        }
        if (n < 0 && errno != EINTR) break;
    }

    close(fd);
    printf("\n[Summary] AvgRTT_ON=%.2fms, AvgRTT_OFF=%.2fms\n", avg_on, avg_off);
    return 0;
}