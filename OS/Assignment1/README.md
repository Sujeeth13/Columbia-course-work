## Part 1 (in the part1 folder):
    - The shell code takes in a line of input from the stdin and runs the command by making various systems calls. The shell can handle any size of input string length but it has a limit of 10 in the number of arguments that can be inputed. If more than 10 arguments are given then an error is printed.
    - Built in executables or any ./exe can be run and this is done by creating a child process that does this execution by making an execv system call. The parent process that created the child process waits for the child to be done executing so that there is no race condition issues.
    - cd command was implemented and only one argument can follow this command. If not an error is printed. 
    - exit command exits the program. It should not have any arguments else an error is printed.
    - Malloc and sys calls like chdir check for return value. In case of failure an error is printed.
### Part 1 Test run
![Part 1 run](https://github.com/W4118/f23-hmwk1-Sujeeth13/tree/main/Images/p1.png)

## Part 2 (in the part2 folder):
    - The working is same as part 1 except three new functions namely cust_printf, cust_malloc and cust_getline functions were implemented and these functions made the system calls like write, mmap and read directly instead of relying on C library implemented functions.
    - cust_printf function writes to any file based on the inputed file descriptor that is given as part of the input. It can print a max of 512 bytes to the file. It also handles only %s format and variable length of arguments can be given based on how many %s are present.
    - cust_malloc allocates the requested length of memory by rounding it off to the nearest multiple of 4096. In case of failure to allocate memory it prints an error.
    - cust_getline functions can read any length of string. If the length of string is more than the buffer size, the buffer size is reallocated with double the memory size and this is done till the entire input string is fed into the buffer.
### Part 2 Test run
![Part 2 run](https://github.com/W4118/f23-hmwk1-Sujeeth13/tree/main/Images/p2.png)

## Part 3 (in the part3 folder):
    - First the entire screen was coloured black.
    - draw_char function is used to draw a char using the font8x8_basic documentation at position x,y passed as arguments.
    - For each character in the "hello, world" draw_char is called to draw the character to the screen.
    - Once the code was compiled and a disk.img was created by running make command we have to boot up a VM in the following way
        - Use the recommended options for creation of OS in the VMware. Options to watch out for is the guest operating system. Select other 64 bit operating system as we are building for x86 arch.
        - Once the VM is created, go to VM settings and in hardware tab add a device called floppy. In this floppy device put the path for the disk.img file and make sure connected at power on is clicked. Make sure the settings are as presented below.
![Part 3 setup](https://github.com/W4118/f23-hmwk1-Sujeeth13/tree/main/Images/p3-add_floppy_device.png)
### Part 3 Test run
![Part 3 run](https://github.com/W4118/f23-hmwk1-Sujeeth13/tree/main/Images/Cust_OS-2023-09-20-18-12-44.png)
