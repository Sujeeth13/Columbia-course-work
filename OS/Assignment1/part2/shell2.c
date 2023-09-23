#define _GNU_SOURCE
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdarg.h>

void* cust_malloc(size_t len){
    if(len == 0)
        return NULL;
    len  = (len + 4095UL) & -4096UL; /* allocating memory block to the nearest 4096 block. */
    void* memory = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE,-1, 0);
    if(memory == MAP_FAILED)
        return NULL;
    return memory;
}

int cust_printf(int fd,const char* buffer,...){
    va_list args;
    char b[4096]; /*buffer to write to the file*/
    va_start(args,buffer); /* to handle variable arguments to print function to allow formating */
    int offset = 0;
    char* ptr;
    char* arg_value;

    while((ptr = strstr(buffer,"%s"))){  /*ensuring we take care of the formating as well*/
        memcpy(b + offset,buffer,ptr - buffer);
        offset += ptr - buffer;
        arg_value = va_arg(args, char*);
        strcpy(b + offset, arg_value);
        offset += strlen(arg_value);
        buffer = ptr + 2;    
    }
    strcpy(b + offset, buffer);
    va_end(args);
    int bytes = write(fd,b,strlen(b));
    return bytes;
}

int cust_getline(char** buffer,size_t* size,int fd){
    size_t bytesRead = 0;
    size_t bytes;
    char ch;
    while ((bytes = read(fd, &ch, 1)) > 0) {
        if (bytesRead >= *size - 1) {
            size_t newBufferSize = *size * 2;
            char *newBuffer = (char *)cust_malloc(newBufferSize);
            if(newBuffer == NULL){
                cust_printf(2,"error: Unable to allocate buffer memory to read input\n");
                return -1;
            }
            memcpy(newBuffer, *buffer, bytesRead);
            munmap(*buffer, *size);

            *buffer = newBuffer;
            *size = newBufferSize;
        }

        if (ch == '\n') {
            break;
        }
        (*buffer)[bytesRead++] = ch;
    }
    (*buffer)[bytesRead] = '\0';

    return bytesRead;
}

int main(){
    char *buffer;
    size_t bufsize = 4096;  /* input buffer size*/
    size_t characters;
    size_t COMMAND_ARGS = 10; /* only 10 arguments allowed in the shell */
    buffer = (char *)cust_malloc(bufsize * sizeof(char));
    if(buffer == NULL){
        cust_printf(2,"error: Unable to allocate buffer");
        exit(1);
    }
    while(1){
        char* input;
        cust_printf(1,"$");
        characters = cust_getline(&buffer,&bufsize,0);
        if(characters == -1){
            cust_printf(2,"error: Error with reading input from stdin\n");
        }

        if(buffer[characters - 1] == '\n'){  /* this condition is to not take \n as an input */
            buffer[characters - 1] = '\0';
        } 

        if(characters == 1 && buffer[characters - 1] == '\0') /* this condition is to handle empty commands */
            continue;

        char* args[COMMAND_ARGS];

        const char delim[2] = " ";
        input = strtok(buffer,delim);
        args[0] = input;
        
        int num_of_args = 1;
        while(input != NULL){
            input = strtok(NULL,delim);
            if (num_of_args == COMMAND_ARGS+1){
                cust_printf(2,"error: Only 10 commands are allowed\n",COMMAND_ARGS);
                break;
            }
            if(input != NULL){
                args[num_of_args] = input;
                num_of_args++;
            }
        }
        if(num_of_args == COMMAND_ARGS+1)
            continue;
        args[num_of_args] = NULL;

        if(strcmp(args[0],"exit") == 0 && num_of_args == 1){
            break;
        }
        else if (strcmp(args[0],"exit") == 0 && num_of_args > 1){
            cust_printf(2,"error: exit can have only one argument\n");
            continue;
        }

        if(strcmp(args[0],"cd") == 0){ /* implementing cd command */
            if(num_of_args != 2){
                cust_printf(2,"error: cd command can have only one argument\n");
            }
            else if(chdir(args[1]) == -1){
                cust_printf(2, "error: %s\n", strerror(errno));
            }
        }
        else if((args[0][0] == '.' && args[0][1] == '/') || args[0][0] == '/'){  /* a child process is created to run exec call only if we are running executables */
            pid_t child_pid;
            child_pid = fork();
            if(child_pid < 0){
                cust_printf(2,"error: Fork failed\n");
                return 1;
            }
            if(child_pid == 0){
                if(execv(args[0],args) == -1){
                    cust_printf(2, "error: %s\n", strerror(errno));
                    return 1;
                }   
            }
            else{
                wait(NULL);
            }

        }
        else{
            cust_printf(2,"error: Command not found\n");
        }
    }
    munmap(buffer,bufsize * sizeof(char)); /*clearing the memory allocated by mmap syscall*/
    return 0;
}
