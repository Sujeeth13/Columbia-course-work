#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

int main(){
    char *buffer;
    size_t bufsize = 4096;
    size_t characters;
    size_t COMMAND_ARGS = 10;
    buffer = (char *)malloc(bufsize * sizeof(char));
    if(buffer == NULL){
        fprintf(stderr,"error: Unable to allocate buffer");
        exit(1);
    }
    while(1){
        char* input;
        fprintf(stdout,"$");
        characters = getline(&buffer,&bufsize,stdin);

        if(characters == -1){
            fprintf(stderr,"error: Error with reading input from stdin\n");
        }

        if(buffer[characters - 1] == '\n'){  /* this condition is to not take \n as an input */
            buffer[characters - 1] = '\0';
        } 

        if(characters == 1 && buffer[characters - 1] == '\0') /* this condition is to handle empty commands */
            continue;

        char* args[COMMAND_ARGS+1];

        const char delim[2] = " ";
        input = strtok(buffer,delim);
        args[0] = input;
        // if(strcmp(input,"exit") == 0){
        //     break;
        // }
        int num_of_args = 1;
        while(input != NULL){
            input = strtok(NULL,delim);
            if (num_of_args == COMMAND_ARGS+1){
                fprintf(stderr,"error: Only %ld commands are allowed\n",COMMAND_ARGS);
                break;
            }
            if(input != NULL){
                args[num_of_args] = input;
                num_of_args++;
            }
        }
        if (num_of_args == COMMAND_ARGS+1)
            continue;
        args[num_of_args] = NULL;

        if(strcmp(args[0],"exit") == 0 && num_of_args == 1){
            break;
        }
        else if (strcmp(args[0],"exit") == 0 && num_of_args > 1){
            fprintf(stderr,"error: exit can have only one argument\n");
            continue;
        }   

        if(strcmp(args[0],"cd") == 0){ /* implementing cd command */
            if(num_of_args != 2){
                fprintf(stderr,"error: cd command can have only one argument\n");
            }
            else if(chdir(args[1]) == -1){
                fprintf(stderr, "error: %s\n", strerror(errno));
            }
        }
        else if((args[0][0] == '.' && args[0][1] == '/') || args[0][0] == '/'){  /* a child process is created to run exec call only if we are running executables */
            pid_t child_pid;
            child_pid = fork();
            if(child_pid < 0){
                fprintf(stderr,"error: Fork failed\n");
                return 1;
            }
            if(child_pid == 0){
                if(execv(args[0],args) == -1){
                    fprintf(stderr, "error: %s\n", strerror(errno));
                    return 1;
                }   
            }
            else{
                wait(NULL);
            }

        }
        else{
            fprintf(stderr,"error: Command not found\n");
        }
    }
    free(buffer);
    return 0;
}
