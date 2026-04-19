// this file is all about what is commonly shared
# ifndef COMMON_H
# define COMMON_H

# include <sys/types.h>

# define MAX_BUFFER 255
# define MAX_CLIENTS 100

typedef struct{
    int ipc_fd;
    int client_id;
    pid_t pid;
}Client;

typedef struct{
    int sender_id;
    // int type; // MESSAGE_BROADCAST OR MESSAGE_PRIVATE
   // int target_id; // Only for MESSAGE_PRIVATE: id of usr to send the message
    char buffer[MAX_BUFFER];
} Message;

# endif