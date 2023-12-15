## Group members:
1. Sujeeth (sb4839)
   - Worked on coding the ptree system call (part 2), part 3 and the part 5.
   - Debugging
   - Hours spent: 20 - 25 hrs.
2. Noam Bechhofer (neb2162)
   - Worked on x86 architecture-specific task processing (getting userpc and
           kernelpc)
   - Debugging for part 3 and 5.
   - checkpatch compliance
   - git merging
   - Hours spent: 20 - 25 hrs.
3. Shayna Gersten (srg2178)
   - Part 4
   - debugging
   - Worked on part 2
   - Hours spent: 15 - 20 hrs.

## Part 2
- The basic workflow for this part is the system call functionality which is defined in linux/kernel/ptree.c file. This system call takes 3 args (root id of process, number of processes to print from the process tree, user space buffer pointer to write to).
-  We have assumed that in the case that the user does not specify the number of requests then we print a max of BUFF_SIZE of nodes set to 1024. The user space code also allows only a max of BUFF_SIZE number of nodes to be printed.
- Traversal through the process tree was done between the rcu_read_lock state to ensure process tree doesn't get updated mid traversal.
- The code checks for various invalid arguments like
    1. NULL pointer for user buffer or nr
    2. nr < 0
    3. if user buf or nr are not part of the user address space
    4. if root_id of process tree doesn't exist.
- The ptree system call prints the process tree in BFS order of the processes and prints the threads associated to each process in ascending order.
- Files modified:
    1. linux/include/uapi/linux/tskinfo.h: this header file was added to define the struct. The struct was used for storing the task info.
    2. linux/arch/x86/entry/syscalls/syscall_64.tbl: This file was modified to add ptree function as the 451 system call in the system call table.
    3. linux/include/linux/syscalls.h: This file was modified to define the ptree function signature.
    4. linux/kernel/Makefile: In the obj-y variable, we added the ptree.o object file to ensure the ptree.c file was compiling and getting linked.
    5. linux/kernel/ptree.c: This file had the functionality of ptree system call which is arch independent.
    6. linux/arch/x86/kernel/ptree.c: This file had the functionality of ptree system call which is arch dependent. The ptree system call which is arch independent makes calls to this file when it has to do anything which is architecture specific.

## Part 3
- The test.c file in user/part3 directory makes the system call to the ptree function using the syscall(451) call.
- If no command line arguments are given then the entire process tree is printed. If only one cmd arg is given then the entire subtree of that root_id is printed. If 2 cmd args are given then nr (cmd arg 2) number of processes are printed of the root_id (cmd arg 1). All error checks are done to ensure the cmd args are valid int inputs.
- The executable prints the process tree if the system call was a success else it prints an error messaged stored in errno.
- we tested the correctness of the system call by verifying the process tree printed with the process tree printed after running the ps -eLf command.

**Test run**
![test_run](https://github.com/W4118/f23-hmwk2-team15/tree/main/Images/ptree.png)
## Part 5
- The foo.c file in user/part5 directory runs a while loop and forks processes that exit immediately till we make a process with pid 5000.
- Once process 5000 is created, this process calls the make_process_tree function that makes the process tree as defined in the problem statement. The parent process of process 5000 exits making process 5000 an orphan and linux makes this process's parent as process 1.
- Finally we run ./test 5000 to print the subtree.
