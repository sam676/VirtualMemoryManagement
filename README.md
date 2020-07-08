# Virtual_Memory_Manager
Operating Systems Project - Virtual Memory Management

A simulation of a Virtual Memory Manager that maps the virtual address spaces of multiple processes onto physical frames. 
The program assumes multiple processes, each with its own virtual address space of 64 virtual pages. 
As the sum of all virtual pages in all virtual address space may exceed the number of physical pages of the simulated system paging needs is implemented. 
The number of physical page frames varies and is specified by a program option, but assumed to support 128 frames. 

INPUT SPECIFICATION:

The input is comprised of:
 1. the number of processes (processes are numbered starting from 0) 
 2. a specification for each process’ address space is comprised of:
    a. the number of virtual memory areas (aka VMAs)
    b. specification for each said VMA comprises of 4 numbers: “starting_virtual_page ending_virtual_page write_protected[0/1] filemapped[0/1]”
