#include <stdio.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

void handle_client(int client_fd, int ipc_fd, int client_id) {
    char buffer[MAX_BUFFER];

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(client_fd, &readfds);  // data from this client
        FD_SET(ipc_fd, &readfds);     // broadcast from parent

        int maxfd = (client_fd > ipc_fd) ? client_fd : ipc_fd;

        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0) {
            perror("select failed in handle_client");
            break;
        }

        // Case 1: This client sent a message — forward to parent as Message struct
        if (FD_ISSET(client_fd, &readfds)) {
            memset(buffer, 0, MAX_BUFFER);
            int n = read(client_fd, buffer, MAX_BUFFER);
            if (n == 0) {
                printf("Client %d disconnected.\n", client_id);
                break;
            }
            if (n < 0) {
                perror("Error reading from client");
                break;
            }
            Message msg;
            msg.sender_id = client_id;
            strncpy(msg.buffer, buffer, MAX_BUFFER - 1);
            write(ipc_fd, &msg, sizeof(msg));  // send to parent for broadcast
        }

        // Case 2: Parent sent a broadcast Message — forward formatted string to this client
        if (FD_ISSET(ipc_fd, &readfds)) {
            Message msg;
            int n = read(ipc_fd, &msg, sizeof(msg));
            if (n <= 0) break;

            char formatted[MAX_BUFFER + 20];
            snprintf(formatted, sizeof(formatted), "Client %d: %s", msg.sender_id, msg.buffer);
            write(client_fd, formatted, strlen(formatted));
        }
    }

    close(client_fd);
    close(ipc_fd);
}