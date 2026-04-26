// this file is all about what is commonly shared
# ifndef COMMON_H
# define COMMON_H

# include <sys/types.h>

# define MAX_BUFFER 255
# define MAX_CLIENTS 50

# define MAX_ROOMS 10
# define MAX_ROOM_NAME 30
# define MAX_USERNAME 30
# define MAX_MEMBERS 12

typedef enum{
    MSG_BROADCAST,
    MSG_PRIVATE,
    MSG_SYSTEM,
    MSG_AUTH,
    MSG_JOIN,
    MSG_LEAVE
}MsgType;

typedef struct{
    int ipc_fd;
    int client_id;
    pid_t pid;
    char username[MAX_USERNAME];
    int authenticated;
}Client;

typedef struct{
    int sender_id;
    int reciever_id; // -1 in case of braodcast
    MsgType type;
    char room_name[MAX_ROOM_NAME];
    char username[MAX_USERNAME];
    char buffer[MAX_BUFFER];
} Message;

// Tracking individual rooms
typedef struct{
    char name[MAX_ROOM_NAME];
    int members[MAX_MEMBERS]; // here we store ids of clients who have joined the room
    int cnt; // tracking the number of memebers present in a chat room
}Room;

# endif