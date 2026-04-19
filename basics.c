# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <unistd.h>
# include <sys/socket.h>
# include <sys/types.h> // data types for os syscall
# include <netinet/in.h>
# include <netdb.h>

int main(){
    int n = 0; 
    // before fork
    printf("Before fork n:%d\n", n);
    sleep(1);
    int pid;
    pid = fork();

    if(pid == 0){
        n = 1;
        printf("\t\t\t\t\t Child Process | PID: %d | n: %d", pid, n);
        while(n < 10){
            printf("\t\t\t\t\t %d \n", n);
            n++;
            sleep(1);
        }
    }
    else{
        printf("\t Parent Process | PID: %d | n: %d", pid, n);
        while(n < 5){
            printf("%d \n", n);
            n+= 10;
            sleep(1);
        }
    }
    sleep(2);
    printf("After Fork PID: %d | n: %d \n", pid, n);
    return 0;
}