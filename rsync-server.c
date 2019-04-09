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
#include <dirent.h>
#include <netinet/in.h>
#include <netinet/sctp.h>
#include "rsync_common.h"

#define BUFFER_SIZE 4096

char *base_dir;  //< The base directory, that all file queries reference to

void send_file_announce(char *filename, int connSock);
void send_file(char *filename, int stream_id, int connSock);

int main()
{
  int listenSock, connSock, ret;
  struct sockaddr_in6 servaddr;
  struct sctp_initmsg sctp_initoptions; 
  time_t currentTime;

  /* Create SCTP TCP-Style Socket */
  listenSock = socket( AF_INET6, SOCK_STREAM, IPPROTO_SCTP );
  debug("Server ready.");
  /* Accept connections from any interface */
  bzero( (void *)&servaddr, sizeof(servaddr) );
  servaddr.sin6_family = AF_INET6;
  servaddr.sin6_addr = in6addr_any;
  servaddr.sin6_port = htons(MY_PORT_NUM);

  bzero( (void *)&sctp_initoptions, sizeof(sctp_initoptions) );
  sctp_initoptions.sinit_num_ostreams = 65535;

  ret = setsockopt(listenSock, SOL_SCTP, SCTP_INITMSG, (void *)&sctp_initoptions, sizeof(sctp_initoptions));
  /* Bind to the wildcard address (all) and MY_PORT_NUM */
  ret = bind( listenSock,
               (struct sockaddr *)&servaddr, sizeof(servaddr) );

  /* Place the server socket into the listening state */
  listen( listenSock, 5 );

  /* Server loop... */
  while( 1 ) {
    int stream_number = 0;
    char *buffer = malloc(BUFFER_SIZE);
    /* Await a new client connection */
    connSock = accept( listenSock,
                        (struct sockaddr *)NULL, (int *)NULL );

    /* New client socket has connected */
    debug("Client connected");
    do {
      struct sctp_sndrcvinfo sndrcvinfo;
      int in, flags;
      /* Read a query */
      in = sctp_recvmsg( connSock, (void *)buffer, BUFFER_SIZE, (struct sockaddr *)NULL, 0, &sndrcvinfo, &flags );
      if (in > 0) switch (*((int *)buffer)) {
        case RSYNC_DIR_LIST_REQ:  // Directory query
        {
          struct rsync_dir_list_msg *query = (struct rsync_dir_list_msg *)buffer;
          char *pathname = &buffer[sizeof(*query)];
          base_dir = strdup(pathname);
          DIR *directory;
          struct dirent *entry;
          int end_message = RSYNC_DIR_NOMOREFILES;
          pathname[query->path_length] = 0; // null-terminate
          debug("* Query directory: %s", pathname);
          directory = opendir(pathname);
          while(entry = readdir(directory)) {
            if(entry->d_type == DT_REG) {
              debug("  - %s", entry->d_name);
              send_file_announce(entry->d_name, connSock);
            }
          }
          debug("(finished)");
          sctp_sendmsg( connSock,  (void *)&end_message, sizeof(end_message), NULL, 0, 0, SCTP_UNORDERED, MASTER_STREAM, 0, 0 );
          break;
        }
        case RSYNC_FILE_REQ:      // File query
        {
          struct rsync_file_msg *query = (struct rsync_file_msg *)buffer;
          char *filename = &buffer[sizeof(*query)];
          filename[query->filename_length] = 0; // null-terminate
          stream_number++;
          /* Read the filename */
          debug("Send file %s on stream %i",filename, stream_number);
          send_file(filename, stream_number, connSock);
          break;
        }
      }
    } while (*((int *)buffer) != RSYNC_GOODBYE);
    /* Close the client connection */
    close( connSock );
    debug("Client disconnected");
    free(buffer);
  }

  return 0;
}

void send_file(char *filename, int stream_id, int connSock)
{
  struct stat file_stats;
  int    ret;
  char   *filepath;

  filepath = malloc(strlen(base_dir) + strlen(filename) + 2);
  strcpy(filepath, base_dir);
  strcat(filepath, "/");
  strcat(filepath, filename);
  ret = stat(filepath, &file_stats);
  if (ret) {
    perror("Error on stat");
    return;
  }
  // Announce the file
  {
    struct rsync_file_send_msg *announcement;
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
    if (announcement->file_length > 0) {
      int    filedesc;
      char   *buffer;
      // Send the file
      buffer = malloc(announcement->file_length+1);
      filedesc = open(filepath, O_RDONLY,0);
      read(filedesc, buffer, announcement->file_length);
      buffer[announcement->file_length] = 0;
      ret = sctp_sendmsg(connSock, buffer, announcement->file_length, NULL, 0, 0, 0, stream_id, 0, 0);
      if (ret < 0) {
        perror("Error while writing on stream");
      }
      debug("Sent file content (%i of %i bytes)", ret, announcement->file_length);
      free(announcement);
      free(buffer);
      close(filedesc);
    } else {
      debug("Empty file");
    }
  }
  free(filepath);
}

void send_file_announce(char *filename, int connSock) {
  struct rsync_file_msg *answer;
  int filename_length = strlen(filename);
  int answer_length = sizeof(*answer) + filename_length;
  answer = malloc(answer_length + 1);
  answer->message_id = RSYNC_DIR_FILE_ANNOUNCE;
  answer->filename_length = filename_length;
  strcpy((char *)answer + sizeof(*answer), filename);
  sctp_sendmsg( connSock, (void *)answer, answer_length, NULL, 0, 0, SCTP_UNORDERED, MASTER_STREAM, 0, 0 );
}