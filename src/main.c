#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 6379

int main() {

  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  char buffer[1024] = {0};

  printf("Logs from your program will appear here!\n");

  int server_fd, client_addr_len, new_server;
  struct sockaddr_in client_addr;

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    printf("Socket creation failed: %s...\n", strerror(errno));
    return 1;
  }

  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
      0) {
    printf("SO_REUSEADDR failed: %s \n", strerror(errno));
    return 1;
  }

  struct sockaddr_in serv_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(PORT),
      .sin_addr = {htonl(INADDR_ANY)},
  };

  if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
    printf("Bind failed: %s \n", strerror(errno));
    return 1;
  }

  int connection_backlog = 5;

  if (listen(server_fd, connection_backlog) != 0) {
    printf("Listen failed: %s \n", strerror(errno));
    return 1;
  }

  printf("Waiting for a client to connect...\n");
  client_addr_len = sizeof(client_addr);

  while (1) {
    if ((new_server = accept(server_fd, (struct sockaddr *)&client_addr,
                             (socklen_t *)&client_addr_len)) == -1) {
      perror("Error");
      return 1;
    }

    while (1) {
      memset(buffer, 0, sizeof(buffer));

      ssize_t n = read(new_server, buffer, sizeof(buffer) - 1);

      if (n <= 0) {
        break;
      }

      const char *ping = "+PONG\r\n";
      send(new_server, ping, strlen(ping), 0);
    }

    close(new_server);
  }

  close(server_fd);

  return 0;
}
