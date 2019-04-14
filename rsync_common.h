#include <stdarg.h>

#define MAX_BUFFER	1024

#define MY_PORT_NUM	19000
#define MASTER_STREAM 0

#define min(a,b) (a<b?a:b)

enum message_ids {
    // Client
    RSYNC_FILE_REQ,
    RSYNC_DIR_LIST_REQ,
    // Server
    RSYNC_FILE_START,
    RSYNC_DIR_FILE_ANNOUNCE,
    RSYNC_DIR_NOMOREFILES,
    RSYNC_GOODBYE = -1
};

// Client messages
struct rsync_file_msg { // - RSYNC_FILE_REQ
   int message_id;      // constant RSYNC_FILE_REQ
   int filename_length; // length of the filename
   // Followed by the filename itself
};

struct rsync_dir_list_msg { // -- RSYNC_DIR_LIST_REQ
   int message_id;      
   int path_length;     // length of the directory name
   // Followed by the directory name
};

// Server messages
// uses also rsync_file_msg -- RSYNC_DIR_FILE_ANNOUNCE
struct rsync_file_send_msg { // -- RSYNC_FILE_START
   int message_id;      
   int stream_id;       // ID on which stream the file is going to be sent
   int mode;            // file mode
   int file_length;     // length of the file
   int filename_length; // length of the filename
   // Followed by the filename itself
};

void debug(char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  putchar('\n');
  va_end(args);
}
