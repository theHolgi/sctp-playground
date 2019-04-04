#define MAX_BUFFER	1024

#define MY_PORT_NUM	19000
#define MASTER_STREAM 0

enum message_ids {
    RSYNC_FILE_REQ = 0,
    RSYNC_FILE_START,
    RSYNC_GOODBYE = -1
};

struct rsync_request_msg {
   int message_id;      // constant RSYNC_FILE_REQ
   int filename_length; // length of the filename
   // Followed by the filename itself
};

struct rsync_announce_msg {
   int message_id;      // constant RSYNC_FILE_START
   int stream_id;       // ID on which stream the file is going to be sent
   int file_length;     // length of the file
   int filename_length; // length of the filename
   // Followed by the filename itself
};

