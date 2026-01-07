#include "server.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#define BUFFER_SIZE 8192
#define BACKLOG 10

void handle_sigchld(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void handle_client(int client_fd, const char* client_ip, int client_port) {
    char buffer[BUFFER_SIZE];

    printf("Client %s:%d connected (pid %d)\n", client_ip, client_port, getpid());

    while (1) {
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            if (bytes_read < 0) perror("read failed");
            else printf("Client %s:%d disconnected\n", client_ip, client_port);
            break;
        }

        const char* pong = "+PONG\r\n";
        if (send(client_fd, pong, strlen(pong), 0) < 0) {
            perror("send failed");
            break;
        }
    }
    close(client_fd);
}

void run_server(int port) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, handle_sigchld);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket creation failed");
        exit(1);
    }

    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(server_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(1);
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(1);
    }

    printf("Waiting for a client to connect...\n");

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (1) {
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept failed");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        int client_port = ntohs(client_addr.sin_port);

        pid_t pid = fork();
        if (pid == 0) {
            close(server_fd);
            handle_client(client_fd, client_ip, client_port);
            exit(0);
        } else if (pid > 0) {
            close(client_fd);
        } else {
            perror("fork failed");
            close(client_fd);
        }
    }
}
