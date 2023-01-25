#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BUF_SIZE 1024

#define SERVICE_READY 220
#define QUITTING 221
#define PASV_CONNECT 227
#define LOGIN_SUCCESS 230
#define PATHNAME_CREATED 257
#define PASSWORD_REQUEST 331
#define FILE_NOT_FOUND 550

#define GET 1
#define PUT 2
#define PWD 3
#define DIR 4
#define CD 5
#define QUIT 6

struct sockaddr_in server;
int data_port;
char user[20];
char pass[20];

uint32_t getHostAddr(const char *host) {
  struct addrinfo hints, *res, *result;
  int errcode;
  char addrstr[100];
  void *ptr;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = PF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags |= AI_CANONNAME;

  errcode = getaddrinfo(host, NULL, &hints, &result);
  if (errcode != 0) {
    perror("getaddrinfo");
    exit(-1);
  }

  res = result;

  inet_ntop(res->ai_family, res->ai_addr->sa_data, addrstr, 100);

  if (res->ai_family == AF_INET6) {
    exit(-1);
  }
  ptr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;

  freeaddrinfo(result);

  return *(uint32_t *)ptr;
}

void sendCommand(int sock_fd, const char *cmd, const char *info) {
  char buf[BUF_SIZE] = {0};
  strcpy(buf, cmd);
  strcat(buf, info);
  strcat(buf, "\r\n");
  if (send(sock_fd, buf, strlen(buf), 0) < 0) {
    perror("send");
    exit(-1);
  }
}

int getReturnCode(int sockfd) {
  int r_code, bytes;
  char buf[BUF_SIZE] = {0}, nbuf[5] = {0};
  if ((bytes = read(sockfd, buf, BUF_SIZE - 2)) > 0) {
    r_code = atoi(buf);
    buf[bytes] = '\0';
    printf("%s", buf);
  } else {
    return -1;
  }

  if (buf[3] == '-') {
    char *newline = strchr(buf, '\n');
    if (*(newline + 1) == '\0') {
      while ((bytes = read(sockfd, buf, BUF_SIZE - 2)) > 0) {
        buf[bytes] = '\0';
        printf("%s", buf);
        if (atoi(buf) == r_code)
          break;
      }
    }
  }

  if (r_code == PASV_CONNECT) {
    char *begin = strrchr(buf, ',') + 1;
    char *end = strrchr(buf, ')');
    strncpy(nbuf, begin, end - begin);
    nbuf[end - begin] = '\0';
    data_port = atoi(nbuf);
    buf[begin - 1 - buf] = '\0';
    end = begin - 1;
    begin = strrchr(buf, ',') + 1;
    strncpy(nbuf, begin, end - begin);
    nbuf[end - begin] = '\0';
    data_port += 256 * atoi(nbuf);
  }

  return r_code;
}

int connectToHost(char *ip, int pt) {
  int sockfd;
  int port = pt;
  if (port <= 0 || port >= 65536) {
    printf("Invalid Port Number");
    exit(-1);
  }
  server.sin_family = AF_INET;
  server.sin_port = htons(port);
  server.sin_addr.s_addr = (uint32_t)getHostAddr(ip);
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    exit(-1);
  }
  if (connect(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0) {
    printf("connect");
    exit(-1);
  }
  printf(
      "\tSuccessful connection to %s:%d\n",
      inet_ntoa(server.sin_addr),
      ntohs(server.sin_port));
  return sockfd;
}

int login(int sockfd) {
  char buf[BUF_SIZE];
  printf("Username: ");
  fgets(buf, sizeof(buf), stdin);
  strncpy(user, buf, strlen(buf) - 1);
  sendCommand(sockfd, "USER ", user);
  if (getReturnCode(sockfd) == PASSWORD_REQUEST) {
    memset(buf, 0, sizeof(buf));
    printf("Password: ");
    fgets(buf, sizeof(buf), stdin);
    strncpy(pass, buf, strlen(buf) - 1);
    sendCommand(sockfd, "PASS ", pass);
    if (getReturnCode(sockfd) != LOGIN_SUCCESS) {
      printf("Wrong password\n");
      return -1;
    } else {
      printf("Login successful\n", user);
      return 0;
    }
  } else {
    printf("User not found! ");
    return -1;
  }
}

int cmdToNum(char *cmd) {
  cmd[strlen(cmd) - 1] = '\0';
  if (strncmp(cmd, "get", 3) == 0)
    return GET;
  if (strncmp(cmd, "put", 3) == 0)
    return PUT;
  if (strcmp(cmd, "pwd") == 0)
    return PWD;
  if (strcmp(cmd, "dir") == 0)
    return DIR;
  if (strncmp(cmd, "cd", 2) == 0)
    return CD;
  if (strcmp(cmd, "quit") == 0)
    return QUIT;
  return -1;
}

void get(int sockfd, char *cmd) {
  int i = 0, data_sock, bytes;
  char filename[BUF_SIZE], buf[BUF_SIZE];
  while (i < strlen(cmd) && cmd[i] != ' ')
    i++;
  if (i == strlen(cmd)) {
    printf("Command error: %s\n", cmd);
    return;
  }
  while (i < strlen(cmd) && cmd[i] == ' ')
    i++;
  if (i == strlen(cmd)) {
    printf("Command error: %s\n", cmd);
    return;
  }
  strncpy(filename, cmd + i, strlen(cmd + i) + 1);

  sendCommand(sockfd, "TYPE ", "I");
  getReturnCode(sockfd);
  sendCommand(sockfd, "PASV", "");
  if (getReturnCode(sockfd) != PASV_CONNECT) {
    printf("Error!\n");
    return;
  }
  server.sin_port = htons(data_port);
  if ((data_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    exit(-1);
  }

  if (connect(data_sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
    perror("connect");
    exit(-1);
  }
  printf(
      "Data connection: %s:%d\n",
      inet_ntoa(server.sin_addr),
      ntohs(server.sin_port));
  sendCommand(sockfd, "RETR ", filename);
  if (getReturnCode(sockfd) == FILE_NOT_FOUND) {
    close(sockfd);
    return;
  }

  FILE *dst_file;
  if ((dst_file = fopen(filename, "wb")) == NULL) {
    printf("Error!");
    close(sockfd);
    return;
  }
  while ((bytes = read(data_sock, buf, BUF_SIZE)) > 0)
    fwrite(buf, 1, bytes, dst_file);

  close(data_sock);
  getReturnCode(sockfd);
  fclose(dst_file);
}

void put(int sockfd, char *cmd) {
  int i = 0, data_sock, bytes;
  char filename[BUF_SIZE], buf[BUF_SIZE];
  while (i < strlen(cmd) && cmd[i] != ' ')
    i++;
  if (i == strlen(cmd)) {
    printf("Command error: %s\n", cmd);
    return;
  }
  while (i < strlen(cmd) && cmd[i] == ' ')
    i++;
  if (i == strlen(cmd)) {
    printf("Command error: %s\n", cmd);
    return;
  }
  strncpy(filename, cmd + i, strlen(cmd + i) + 1);

  sendCommand(sockfd, "PASV", "");
  if (getReturnCode(sockfd) != PASV_CONNECT) {
    printf("Error!");
    return;
  }
  FILE *src_file;
  if ((src_file = fopen(filename, "rb")) == NULL) {
    printf("Error!");
    return;
  }
  server.sin_port = htons(data_port);
  if ((data_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    exit(-1);
  }
  if (connect(data_sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
    perror("connect");
    exit(-1);
  }
  printf(
      "Data connection: %s:%d\n",
      inet_ntoa(server.sin_addr),
      ntohs(server.sin_port));
  sendCommand(sockfd, "STOR ", filename);
  if (getReturnCode(sockfd) == FILE_NOT_FOUND) {
    close(data_sock);
    fclose(src_file);
    return;
  }
  while ((bytes = fread(buf, 1, BUF_SIZE, src_file)) > 0)
    send(data_sock, buf, bytes, 0);

  close(data_sock);
  getReturnCode(sockfd);
  fclose(src_file);
}

void pwd(int sockfd) {
  sendCommand(sockfd, "PWD", "");
  if (getReturnCode(sockfd) != PATHNAME_CREATED)
    exit(-1);
}

void dir(int sockfd) {
  int data_sock, bytes;
  char buf[BUF_SIZE] = {0};
  sendCommand(sockfd, "PASV", "");
  if (getReturnCode(sockfd) != PASV_CONNECT) {
    printf("Error!");
    return;
  }
  server.sin_port = htons(data_port);
  if ((data_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    exit(-1);
  }
  if (connect(data_sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
    perror("connect");
    exit(-1);
  }
  printf(
      "Data connection: %s:%d\n",
      inet_ntoa(server.sin_addr),
      ntohs(server.sin_port));

  sendCommand(sockfd, "LIST ", "-al");
  getReturnCode(sockfd);
  printf("\n");

  while ((bytes = read(data_sock, buf, BUF_SIZE - 2)) > 0) {
    buf[bytes] = '\0';
    printf("%s", buf);
  }
  printf("\n");
  close(data_sock);
  getReturnCode(sockfd);
}

void cd(int sockfd, char *cmd) {
  int i = 0;
  char buf[BUF_SIZE];
  while (i < strlen(cmd) && cmd[i] != ' ')
    i++;
  if (i == strlen(cmd)) {
    printf("Command error: %s\n", cmd);
    return;
  }
  while (i < strlen(cmd) && cmd[i] == ' ')
    i++;
  if (i == strlen(cmd)) {
    printf("Command error: %s\n", cmd);
    return;
  }
  strncpy(buf, cmd + i, strlen(cmd + i) + 1);
  sendCommand(sockfd, "CWD ", buf);
  getReturnCode(sockfd);
}

void cmd_quit(int sockfd) {
  sendCommand(sockfd, "QUIT", "");
  if (getReturnCode(sockfd) == QUITTING)
    printf("Logout.\n");
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf("Usage: %s <host>\n", argv[0]);
    exit(-1);
  }
  int sockfd = connectToHost(argv[1], 21);
  if (getReturnCode(sockfd) != SERVICE_READY) {
    perror("Service Error!");
    exit(-1);
  }
  while (login(sockfd) != 0) {
  }
  int timeToQuit = 0;
  char buf[BUF_SIZE];
  while (!timeToQuit) {
    printf(":");
    fgets(buf, sizeof(buf), stdin);
    switch (cmdToNum(buf)) {
    case GET:
      get(sockfd, buf);
      break;
    case PUT:
      put(sockfd, buf);
      break;
    case PWD:
      pwd(sockfd);
      break;
    case DIR:
      dir(sockfd);
      break;
    case CD:
      cd(sockfd, buf);
      break;
    case QUIT:
      cmd_quit(sockfd);
      timeToQuit = 1;
      break;
    default:
      printf("COMMANDS: get put pwd cd dir quit\n");
      break;
    }
  }
  close(sockfd);
}
