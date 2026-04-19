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

void error(const char* msg){ // prompts the error to the client and terminates
    perror(msg);
    exit(1);
}

int main(int argv, char* argc[]){
    if(argv < 3){
        error("No Port no provided...\nTerminating the program");
    }

    int sockfd, portno, n;
    struct sockaddr_in ser_add;
    char buffer[MAX_BUFFER];

    struct hostent *server;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    portno = atoi(argc[2]);

    if(sockfd < 0){
        error("Error in Creating Socket...");
    }

    server = gethostbyname(argc[1]);
    if(server == NULL) {
        error("No server provided...");
    }

    bzero((char *)&ser_add, sizeof(ser_add));

    ser_add.sin_family = AF_INET;
    memcpy(&ser_add.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
    ser_add.sin_port = htons(portno);

    int p = connect(sockfd, (struct sockaddr*)&ser_add, sizeof(ser_add));

    if(p < 0){
        error("Error occured while connecting to server.....");
    }

    while(1){
        fd_set readfds;
        FD_ZERO(&readfds);

        FD_SET(0, &readfds); // stdin (keyboard and monitor)
        FD_SET(sockfd, &readfds); // add sockfd to vector of bits

        int maxfd = sockfd;

        if(select(maxfd+1, &readfds, NULL, NULL, NULL) < 0){
            error("Error caused due to select...");
        }

        // Case 1: User giving input 
        if(FD_ISSET(0, &readfds)){
            memset(buffer, 0, MAX_BUFFER);
            fgets(buffer, sizeof(buffer), stdin);  // NO printf before this
            printf("\033[A\r\033[K");              // go up one line, erase the echoed input
            printf("\033[33mYou: %s\033[0m", buffer);  // reprint cleanly in yellow
            fflush(stdout);
            write(sockfd, buffer, strlen(buffer));
            if(!strncmp("Bye", buffer, 3)) break;
            printf("You: ");   // prompt for next input
            fflush(stdout);
        }

        // Case 2: server sent something 
        if(FD_ISSET(sockfd, &readfds)){
            memset(buffer, 0, MAX_BUFFER);
            int n = read(sockfd, buffer, sizeof(buffer));
            if(n == 0) error("Server Disconnected...");
            if(n < 0) error("Error occured while reading..");
            printf("\r\033[K");                         // clear current You: prompt line
            printf("\033[32m%s\033[0m\n", buffer);      // print received msg in green
            printf("You: ");                            // reprint prompt
            fflush(stdout);
        }
    }
    close(sockfd);
    return 0;

}