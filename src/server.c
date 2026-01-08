#include "server.h"
#include "parser.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUFFER_SIZE 8192
#define BACKLOG 10

void handle_sigchld(int sig) {
  (void)sig;
  while (waitpid(-1, NULL, WNOHANG) > 0)
    ;
}

static void send_error(int fd, const char *msg) {
  char buf[256];
  int len = snprintf(buf, sizeof(buf), "-ERR %s\r\n", msg);
  send(fd, buf, len, 0);
}

static void send_simple_string(int fd, const char *str) {
  char buf[256];
  int len = snprintf(buf, sizeof(buf), "+%s\r\n", str);
  send(fd, buf, len, 0);
}

static void send_bulk_string(int fd, const char *str, size_t len) {
  char header[32];
  snprintf(header, sizeof(header), "$%zu\r\n", len);
  send(fd, header, strlen(header), 0);
  send(fd, str, len, 0);
  send(fd, "\r\n", 2, 0);
}

typedef void (*cmd_handler_fn)(int fd, redis_reply *cmd);

static void cmd_ping(int fd, redis_reply *cmd) {
  (void)cmd;
  send_simple_string(fd, "PONG");
}

static void cmd_echo(int fd, redis_reply *cmd) {
  if (cmd->elements_count < 2) {
    send_error(fd, "wrong number of arguments for 'echo' command");
    return;
  }
  redis_reply *arg = cmd->elements[1];
  send_bulk_string(fd, arg->str, arg->len);
}

typedef struct {
  const char *name;
  cmd_handler_fn handler;
} cmd_entry;

static cmd_entry commands[] = {
    {"PING", cmd_ping}, {"ECHO", cmd_echo}, {NULL, NULL}};

static void handle_command(int client_fd, redis_reply *cmd) {
  if (cmd->type != REDIS_REPLY_ARRAY || cmd->elements_count < 1) {
    send_error(client_fd, "invalid command");
    return;
  }

  redis_reply *cmd_name = cmd->elements[0];
  if (cmd_name->type != REDIS_REPLY_STRING) {
    send_error(client_fd, "invalid command");
    return;
  }

  for (cmd_entry *e = commands; e->name != NULL; e++) {
    if (strcasecmp(cmd_name->str, e->name) == 0) {
      e->handler(client_fd, cmd);
      return;
    }
  }

  send_error(client_fd, "unknown command");
}

void handle_client(const int client_fd, const char *client_ip,
                   const int client_port) {
  char buffer[BUFFER_SIZE];
  redis_reader *reader = redis_reader_create();

  printf("Client %s:%d connected (pid %d)\n", client_ip, client_port, getpid());

  while (1) {
    memset(buffer, 0, BUFFER_SIZE);
    const ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);

    if (bytes_read <= 0) {
      if (bytes_read < 0)
        perror("read failed");
      else
        printf("Client %s:%d disconnected\n", client_ip, client_port);
      break;
    }

    redis_reader_feed(reader, buffer, bytes_read);

    redis_reply *reply;
    while (redis_reader_get_reply(reader, &reply) == REDIS_OK && reply) {
      handle_command(client_fd, reply);
      redis_reply_free(reply);
    }
  }

  redis_reader_free(reader);
  close(client_fd);
}

void run_server(const int port) {
  signal(SIGPIPE, SIG_IGN);
  signal(SIGCHLD, handle_sigchld);

  const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    perror("socket creation failed");
    exit(1);
  }

  const int reuse = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_in serv_addr = {.sin_family = AF_INET,
                                  .sin_port = htons(port),
                                  .sin_addr.s_addr = INADDR_ANY};

  if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
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
    const int client_fd =
        accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
      perror("accept failed");
      continue;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    const int client_port = ntohs(client_addr.sin_port);

    const pid_t pid = fork();
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
