#ifndef FILE_H
#define FILE_H

#include <stdint.h>

// Define the file structure
typedef struct {
    char name[256];      // File name
    char extension[16];  // File extension
    uint8_t type;        // File type (e.g., 0 = regular file, 1 = directory)
    uint64_t size;       // File size in bytes
    uint32_t inode;      // Inode number
} file_t;

#endif // FILE_H