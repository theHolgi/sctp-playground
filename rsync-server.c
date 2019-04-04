#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/sctp.h>
#include "rsync_common.h"

void send_file(char *filename, int stream_id, int connSock);

int main()
{
  int listenSock, connSock, ret;
  struct sockaddr_in6 servaddr;
  time_t currentTime;

  /* Create SCTP TCP-Style Socket */
  listenSock = socket( AF_INET6, SOCK_STREAM, IPPROTO_SCTP );

  /* Accept connections from any interface */
  bzero( (void *)&servaddr, sizeof(servaddr) );
  servaddr.sin6_family = AF_INET6;
  servaddr.sin6_addr = in6addr_any;
  servaddr.sin6_port = htons(MY_PORT_NUM);

  /* Bind to the wildcard address (all) and MY_PORT_NUM */
  ret = bind( listenSock,
               (struct sockaddr *)&servaddr, sizeof(servaddr) );

  /* Place the server socket into the listening state */
  listen( listenSock, 5 );

  /* Server loop... */
  while( 1 ) {
    int stream_number = 0;
    struct rsync_request_msg query_message;
    /* Await a new client connection */
    connSock = accept( listenSock,
                        (struct sockaddr *)NULL, (int *)NULL );

    /* New client socket has connected */
    do {
      struct sctp_sndrcvinfo sndrcvinfo;
      int in, flags;
      /* Read a query */
      in = sctp_recvmsg( connSock, (void *)&query_message, sizeof(query_message),
                        (struct sockaddr *)NULL, 0, &sndrcvinfo, &flags );
      if (in > 0 && query_message.message_id == RSYNC_FILE_REQ) {
        char *filename = malloc(query_message.filename_length + 1);
        /* Read the filename */
        in = sctp_recvmsg( connSock, (void *)filename, query_message.filename_length,
                        (struct sockaddr *)NULL, 0, &sndrcvinfo, &flags );
        stream_number++;
        filename[query_message.filename_length] = 0;
        printf("Send file %s on stream %i\n",filename, stream_number);
        send_file(filename, stream_number, connSock);
        free(filename);
      }
    } while (query_message.message_id != RSYNC_GOODBYE);
    /* Close the client connection */
    close( connSock );
  }

  return 0;
}

void send_file(char *filename, int stream_id, int connSock)
{
  struct stat file_stats;
  int    ret;
  int    filedesc;
  char   *buffer;

  ret = stat(filename, &file_stats);
  if (ret) perror("Error on stat");

  filedesc = open(filename, O_RDONLY,0);
  // Announce the file
  {
    struct rsync_announce_msg *announcement;
    size_t  announcement_length;
    announcement_length = sizeof(*announcement) + strlen(filename);
    announcement = malloc(announcement_length+1); // Reserve space for message, filename and null-termination
    announcement->message_id =  RSYNC_FILE_START;
    announcement->stream_id  = stream_id;
    announcement->filename_length = strlen(filename);
    announcement->file_length = file_stats.st_size;
    strcpy((char *)announcement + sizeof(*announcement), filename);
    ret = sctp_sendmsg( connSock,
                    (void *)announcement, announcement_length, NULL, 0, 0, SCTP_UNORDERED, MASTER_STREAM, 0, 0 );
    if (ret < 0) perror("Error while sending answer");
    // Send the file
    buffer = malloc(announcement->file_length+1);
    read(filedesc, buffer, announcement->file_length);
    buffer[announcement->file_length] = 0;
    ret = sctp_sendmsg(connSock, buffer, announcement->file_length, NULL, 0, 0, 0, stream_id, 0, 0);
    printf("Sent file content: %s\n", buffer);
    free(announcement);
  }
  free(buffer);
  close(filedesc);
}
