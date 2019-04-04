#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/sctp.h>
#include <arpa/inet.h>
#include "rsync_common.h"

int main()
{
  int connSock, in, flags;
  struct sockaddr_in6 servaddr;
  struct sctp_sndrcvinfo sndrcvinfo;
  struct sctp_event_subscribe events;

  int goodbye_msg = RSYNC_GOODBYE;

  /* Create an SCTP TCP-Style Socket */
  connSock = socket( AF_INET6, SOCK_STREAM, IPPROTO_SCTP );

  /* Specify the peer endpoint to which we'll connect */
  bzero( (void *)&servaddr, sizeof(servaddr) );
  servaddr.sin6_family = AF_INET6;
  servaddr.sin6_port = htons(MY_PORT_NUM);
  servaddr.sin6_addr = in6addr_loopback;

  /* Connect to the server */
  connect( connSock, (struct sockaddr *)&servaddr, sizeof(servaddr) );

  /* Enable receipt of SCTP Snd/Rcv Data via sctp_recvmsg */
  memset( (void *)&events, 0, sizeof(events) );
  events.sctp_data_io_event = 1;
  setsockopt( connSock, SOL_SCTP, SCTP_EVENTS,
               (const void *)&events, sizeof(events) );

  /* Send a file request */
  {
    int i;
    struct rsync_request_msg  *query_message;
    struct rsync_announce_msg answer_message;
    char *filename = "test.txt";
    char *buffer;
    int message_length;

    message_length = sizeof(query_message) + strlen(filename);
    query_message = malloc(message_length + 1); // Reserve enough space for message and filename and null-termination
    query_message->message_id  = RSYNC_FILE_REQ;
    query_message->filename_length = strlen(filename);
    strcpy((char *)query_message + sizeof(*query_message), filename);
    in = sctp_sendmsg( connSock, (void *)query_message, message_length, NULL, 0, 0, SCTP_UNORDERED, MASTER_STREAM, 0, 0 );
    free(query_message);
    in = sctp_recvmsg( connSock, (void *)&answer_message, sizeof(answer_message), NULL, 0, &sndrcvinfo, &flags );
    if (sndrcvinfo.sinfo_stream == MASTER_STREAM) {
      size_t buf_size = answer_message.filename_length;
      if (buf_size < answer_message.file_length) buf_size = answer_message.file_length;
      buffer = malloc(++buf_size);
      for (i=0; i<2; i++) {
        in = sctp_recvmsg( connSock, (void *)buffer, buf_size, (struct sockaddr *)NULL, 0, &sndrcvinfo, &flags );
        if (sndrcvinfo.sinfo_stream == MASTER_STREAM) {
          buffer[answer_message.filename_length] = 0;
          printf("Reveived: file %s length %d\n", filename, answer_message.file_length);
          filename = malloc(answer_message.filename_length+1);
          strncpy(filename, buffer, answer_message.filename_length);
        } else {
          int fh;
          buffer[answer_message.file_length] = 0;
          printf(" file content on stream %d\n", sndrcvinfo.sinfo_stream);
          fh = open(filename, O_CREAT|O_WRONLY, 0666);
          write(fh, buffer, answer_message.file_length);
          close(fh);
        }
      }
      free(filename);
      free(buffer);
    }
  }

  /* Close our socket and exit */
  in = sctp_sendmsg(connSock, (void *)&goodbye_msg, sizeof(goodbye_msg), NULL, 0, 0, 0, MASTER_STREAM, 0, 0); // bye
  in = sctp_sendmsg(connSock, NULL, 0, NULL, 0, 0, SCTP_EOF, MASTER_STREAM, 0, 0); // shutdown
  close(connSock);

  return 0;
}