#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>

#define MAXLINE 1024
#define SERVER_PORT 12345

void handle_post(int sockfd, const char *message) {
    char buffer[MAXLINE];
    snprintf(buffer, sizeof(buffer), "POST %s", message);
    send(sockfd, buffer, strlen(buffer), 0);
}

void handle_get(int sockfd) {
    char buffer[MAXLINE];
    send(sockfd, "GET", 3, 0);
    int n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        printf("Messages:\n%s\n", buffer);
    } else {
        perror("recv");
    }
}

int main(int argc, char **argv) {
    int sockfd;
    struct sockaddr_in servaddr;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    char command[MAXLINE];
    while (1) {
        printf("Enter command (POST <message> / GET / EXIT): ");
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = 0; // Remove newline

        if (strncmp(command, "POST", 4) == 0) {
            handle_post(sockfd, command + 5);
        } else if (strcmp(command, "GET") == 0) {
            handle_get(sockfd);
        } else if (strcmp(command, "EXIT") == 0) {
            break;
        } else {
            printf("Invalid command. Please use POST, GET, or EXIT.\n");
        }
    }

    close(sockfd);
    return 0;
}