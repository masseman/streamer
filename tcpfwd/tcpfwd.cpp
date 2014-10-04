#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define BUFFER_SIZE 65536

namespace
{
  void error(const char *msg)
  {
    perror(msg);
    exit(1);
  }

  int max_silence_seconds;

  int sockfd = 0;
  int newsockfd = 0;

  uint32_t tick = 0;
  uint32_t last_read_tick = 0;

  void* checkTimeout(void*)
  {
    while (true)
    {
      uint32_t current_tick = __sync_add_and_fetch(&tick, 1);
      uint32_t current_last_read_tick =
        __sync_add_and_fetch(&last_read_tick, 0);

      if (current_last_read_tick > 0)
      {
        uint32_t diff = current_tick - current_last_read_tick;
        if (diff > max_silence_seconds)
        {
          close(newsockfd);
          close(sockfd);
          exit(0);
        }
      }

      usleep(1000000);
    }
  }
}

int main(int argc, char *argv[])
{
  if (argc < 3) {
    fprintf(stderr, "Usage: <port> <timeout (seconds)>\n");
    exit(1);
  }

  max_silence_seconds = atoi(argv[2]);

  pthread_t timeout_thread;
  if (pthread_create(&timeout_thread, 0, checkTimeout, 0))
  {
    error("Error creating thread");
    return 1;
  }

  int portno;
  socklen_t clilen;
  char buffer[BUFFER_SIZE];
  struct sockaddr_in serv_addr, cli_addr;
  int n;
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
    error("ERROR opening socket");

  int reuseaddr = 1;
  if (setsockopt(sockfd,
                 SOL_SOCKET,
                 SO_REUSEADDR,
                 &reuseaddr,
                 sizeof(reuseaddr)) == -1)
  {
    error("Setting SO_REUSEADDR");
    return 1;
  }

  bzero((char *) &serv_addr, sizeof(serv_addr));
  portno = atoi(argv[1]);
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(portno);
  if (bind(sockfd, (struct sockaddr *) &serv_addr,
           sizeof(serv_addr)) < 0)
    error("ERROR on binding");
  listen(sockfd,5);

  clilen = sizeof(cli_addr);
  newsockfd = accept(sockfd,
                     (struct sockaddr *) &cli_addr,
                     &clilen);
  if (newsockfd < 0)
    error("ERROR on accept");

  while (true)
  {
    int n = read(newsockfd, buffer, BUFFER_SIZE);
    if (n <= 0 || write(STDOUT_FILENO, buffer, n) <= 0)
      break;

    while (true)
    {
      uint32_t new_last_read_tick = __sync_add_and_fetch(&tick, 0);
      uint32_t current_last_read_tick = __sync_add_and_fetch(&last_read_tick, 0);
      if (__sync_val_compare_and_swap(&last_read_tick,
                                      current_last_read_tick,
                                      new_last_read_tick) ==
          new_last_read_tick)
      {
        break;
      }
    }
  }

  close(newsockfd);
  close(sockfd);
  return 0;
}
