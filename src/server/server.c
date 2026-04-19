# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <unistd.h>
# include <sys/types.h>
# include <sys/select.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <netdb.h>
# include "common.h"
# include "client_handler.h"

void error(const char* msg){ // prompts the error to the client and terminates
    perror(msg);
    exit(1);
}

int main(int argc, char* argv[]){
    if(argc < 2){ // if no ports assigned raise error and terminate
        error("No Port Assigned..\nTerminating the session");
    }

    int port = atoi(argv[1]); // convert from string to int
    int sockfd, newsockfd; //sockets

    char buffer[255];
    struct sockaddr_in addr_ser, addr_cli;
    socklen_t clilen; // used to represent the length of the socket data type

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if(sockfd < 0){
        error("Error ocurred while opening Socket....");
    }

    addr_ser.sin_family = AF_INET;
    addr_ser.sin_addr.s_addr = INADDR_ANY;
    addr_ser.sin_port = htons(port);

    int p = bind(sockfd, (struct sockaddr*) &addr_ser, sizeof(addr_ser));
    if(p < 0){
        error("Binding Failed");
    }

    listen(sockfd, 5);
    clilen = sizeof(addr_cli);
    static int next_client_id = 1;
    int cnt = 0;
    Client client_list[MAX_CLIENTS];
    fd_set readfds;

    while(1){

        FD_ZERO(&readfds);

        FD_SET(sockfd, &readfds);
        int maxfd = sockfd;

        // add all client ipc fds
        for(int i = 0; i < cnt; i++){
            FD_SET(client_list[i].ipc_fd, &readfds);
            if(client_list[i].ipc_fd > maxfd)
                maxfd = client_list[i].ipc_fd;
        }

        // wait for activity
        if(select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0){
            error("select failed");
        }

        // 🔹 NEW CLIENT
        if(FD_ISSET(sockfd, &readfds)){
            newsockfd = accept(sockfd, (struct sockaddr*)&addr_cli, &clilen);

            int id = next_client_id++;

            int sp[2];
            socketpair(AF_UNIX, SOCK_STREAM, 0, sp);

            pid_t pid = fork();

            if(pid == 0){
                close(sockfd);
                close(sp[0]);

                handle_client(newsockfd, sp[1], id);
                exit(0);
            }
            else{
                close(newsockfd);
                close(sp[1]);

                Client c;
                c.client_id = id;
                c.ipc_fd = sp[0];
                c.pid = pid;

                client_list[cnt++] = c;
            }
        }

        // 🔹 MESSAGE FROM CHILD
        for(int i = 0; i < cnt; i++){

            if(FD_ISSET(client_list[i].ipc_fd, &readfds)){

                Message msg;
                int n = read(client_list[i].ipc_fd, &msg, sizeof(msg));

                if(n <= 0){
                    continue;
                }

                for(int j = 0; j < cnt; j++){
                    if(j != i){
                        write(client_list[j].ipc_fd, &msg, sizeof(msg));
                    }
                }
            }
        }
    }
   // close(sockfd);
    // close(newsockfd);
    return 0;
}