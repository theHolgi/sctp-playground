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

#define BUFFER_SIZE 4096

struct file_handle_buf {
  int stream_id;
  int fh;
  int bytes_to_write;
  struct file_handle_buf *next;
};
typedef struct file_handle_buf* file_handle_buf_t;

struct {
  int no_more_files;
  int files_requested;
  int files_in_flight;
} busy_flags;

void init_busy_flags(void) {
  busy_flags.no_more_files = 0;
  busy_flags.files_requested = 0;
  busy_flags.files_in_flight = 0;
}
int query_finished(void) {
  return
    busy_flags.no_more_files && 
    !busy_flags.files_requested &&
    !busy_flags.files_in_flight;
}

file_handle_buf_t FILE_HANDLE_BUFFERS = NULL;

/** Creates a new buffer and returns its pointer */
file_handle_buf_t add_buffer() {
  file_handle_buf_t element = FILE_HANDLE_BUFFERS;
  file_handle_buf_t new_element = malloc(sizeof(file_handle_buf_t[0]));
  new_element->next = NULL;
  if (FILE_HANDLE_BUFFERS == NULL) { // first element
    FILE_HANDLE_BUFFERS = new_element;
  } else {
    while(element->next != NULL) element = element->next;
    element->next = new_element;
  }
  return new_element;
}

/** Finds the buffer with the given ID */
file_handle_buf_t find_buffer(int stream_id) {
  file_handle_buf_t element = FILE_HANDLE_BUFFERS;
  while(element->stream_id != stream_id && element->next) element = element->next;
  return element;
}

/** Deletes the buffer with the given ID */
void del_buffer(file_handle_buf_t buf) {
  if (FILE_HANDLE_BUFFERS == buf) {
    FILE_HANDLE_BUFFERS = buf->next;
  } else {
    file_handle_buf_t element = FILE_HANDLE_BUFFERS;
    while(element->next != buf && element->next) element = element->next;
    element->next = buf->next;
  }
  free(buf);
}

int server_connect() {
  /* Create an SCTP TCP-Style Socket */
  int connSock = socket( AF_INET6, SOCK_STREAM, IPPROTO_SCTP );
  struct sockaddr_in6 servaddr;
  /* Specify the peer endpoint to which we'll connect */
  bzero( (void *)&servaddr, sizeof(servaddr) );
  servaddr.sin6_family = AF_INET6;
  servaddr.sin6_port = htons(MY_PORT_NUM);
  servaddr.sin6_addr = in6addr_loopback;

  /* Connect to the server */
  connect( connSock, (struct sockaddr *)&servaddr, sizeof(servaddr) );

  return connSock;
}

void server_disconnect(int connSock) {
  int goodbye_msg = RSYNC_GOODBYE;
  sctp_sendmsg(connSock, (void *)&goodbye_msg, sizeof(goodbye_msg), NULL, 0, 0, 0, MASTER_STREAM, 0, 0); // bye
  sctp_sendmsg(connSock, NULL, 0, NULL, 0, 0, SCTP_EOF, MASTER_STREAM, 0, 0); // shutdown
  close(connSock);
}

int send_dir_browse(int socket, const char* directory) {
  struct rsync_dir_list_msg *query;
  int    directory_length = strlen(directory);
  int    bytes;
  int    message_length = sizeof(*query) + directory_length;
  query = malloc(message_length+1);

  query->message_id = RSYNC_DIR_LIST_REQ;
  query->path_length = directory_length;  
  strcpy((char *)query + sizeof(*query), directory);
  bytes = sctp_sendmsg( socket, (void *)query, message_length, NULL, 0, 0, SCTP_UNORDERED, MASTER_STREAM, 0, 0 );

  free(query);
  return bytes;
}

int send_file_req(int socket, const char* filename) {
  struct rsync_file_msg *query;
  int    filename_length = strlen(filename);
  int    bytes;
  int    message_length = sizeof(*query) + filename_length;
  query = malloc(message_length+1);

  query->message_id = RSYNC_FILE_REQ;
  query->filename_length = filename_length;  
  strcpy((char *)query + sizeof(*query), filename);
  bytes = sctp_sendmsg( socket, (void *)query, message_length, NULL, 0, 0, SCTP_UNORDERED, MASTER_STREAM, 0, 0 );

  free(query);
  return bytes;
}

int main(int argc, char **argv)
{
  int in, flags, connSock;
  struct sctp_sndrcvinfo sndrcvinfo;
  struct sctp_event_subscribe events;
  const char *dirname = argv[1];
  char*  buffer;

  if (argc < 1 || strlen(dirname) == 0) {
    fputs("Missing argument: directory name\n", stderr);
    exit(1);
  }

  connSock = server_connect();
  buffer = malloc(BUFFER_SIZE);
  /* Enable receipt of SCTP Snd/Rcv Data via sctp_recvmsg */
  memset( (void *)&events, 0, sizeof(events) );
  events.sctp_data_io_event = 1;
  setsockopt( connSock, SOL_SCTP, SCTP_EVENTS, (const void *)&events, sizeof(events) );

  /* Send the directory browse request */
  debug("Querying directory: %s", dirname);
  send_dir_browse(connSock, dirname);

  init_busy_flags();
  do {
    in = sctp_recvmsg( connSock, (void *)buffer, BUFFER_SIZE, (struct sockaddr *)NULL, 0, &sndrcvinfo, &flags );
    if (in < 0) {
      perror("Socket receive");
      exit(1);
    }
    if (sndrcvinfo.sinfo_stream == MASTER_STREAM) {  // control messages
      switch (*((int *)buffer)) {
        case RSYNC_DIR_FILE_ANNOUNCE:
        {
          struct rsync_file_msg *answer = (struct rsync_file_msg *)buffer;
          char *filename = &buffer[sizeof(*answer)];
          filename[answer->filename_length] = 0;
          debug("- dir answer: %s", filename);
          /* Send a file request */
          send_file_req(connSock, filename);
          debug("Request: %s", filename);
          busy_flags.files_requested++;
          break;
        }
        case RSYNC_FILE_START:
        {
          struct rsync_file_send_msg *answer =  (struct rsync_file_send_msg *)buffer;
          char*  filename = &buffer[sizeof(*answer)];
          file_handle_buf_t handle = add_buffer();
          filename[answer->filename_length] = 0;
          debug("Start: file %s length %d", filename, answer->file_length);
          handle->stream_id = answer->stream_id;
          handle->bytes_to_write = answer->file_length;
          handle->fh = open(filename, O_CREAT|O_WRONLY, 0666);
          if (answer->file_length > 0) {
            busy_flags.files_in_flight++;
          } else {
            close(handle->fh);
            del_buffer(handle);
          }
          busy_flags.files_requested--;
          break;
        }
        case RSYNC_DIR_NOMOREFILES:
          debug("No more files.");
          busy_flags.no_more_files = 1;
          break;
      }
    } else {  // We have file content on one of the streams
      file_handle_buf_t handle = find_buffer(sndrcvinfo.sinfo_stream);
      debug("File content stream %i (%i bytes)",sndrcvinfo.sinfo_stream, in);
      write(handle->fh, buffer, in);
      handle->bytes_to_write -= in;
      if (handle->bytes_to_write <= 0) {
        close(handle->fh);
        del_buffer(handle);
        busy_flags.files_in_flight--;
      }
    }
    debug("[ Listing finished: %i, Requested: %i, in flight: %i]", busy_flags.no_more_files, busy_flags.files_requested, busy_flags.files_in_flight);
  } while (!query_finished());

  free(buffer);
  /* Close our socket and exit */
  server_disconnect(connSock);

  return 0;
}