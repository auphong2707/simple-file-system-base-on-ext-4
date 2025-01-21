# Simple File System Based on EXT-4

This project implements a simplified file system inspired by the EXT-4 filesystem structure. It provides essential file and directory management functionalities, such as creating, reading, writing, and removing files and directories. Designed for learning and experimentation, the project offers a hands-on approach to understanding file system concepts and operations through a command-line interface.

# Command Line File System Interface

To run the file system, use the following command:  
```bash
gcc src/main.c -o obj/main.o && obj/main.o
```

## Features

### Directory Commands
- `ls`: List directory contents.
- `pwd`: Show the current directory path.
- `cd <dirname>`: Change the working directory.
- `mkdir <dirname>`: Create a new directory.

### File Commands
- `cf <filename> <data>`: Create a file with specified content.
- `rf <filename>`: Read file content.
- `wf <-a/-o> <filename> <new_content>`: Append (`-a`) or overwrite (`-o`) file content.
- `rm <-f/-d> <filename>`: Remove a file (`-f`) or directory (`-d`).

### System Commands
- `test`: Run file system evaluation tests.
- `exit`: Exit the program.

## Example Usage
```bash
cf example.txt "Hello, World!"   # Create a file with content
rf example.txt                   # Read file content
wf -a example.txt "New text"     # Append content to file
ls                               # List directory contents
mkdir new_folder                 # Create a new directory
cd new_folder                    # Change to the directory
rm -f example.txt                # Delete the file
