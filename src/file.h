#ifndef FILE_H
#define FILE_H

#include <stdint.h>
#include <time.h>

#define MAX_FILENAME_LEN 255

// Define the file structure
typedef struct {
    char name[256];      // File name
    char extension[16];  // File extension
    uint8_t type;        // File type (e.g., 0 = regular file, 1 = directory)
    uint64_t size;       // File size in bytes
    uint32_t inode;      // Inode number
} file_t;

// Directory entry in ext4-like format (highly simplified)
typedef struct dir_entry {
    uint32_t inode;         // Inode number
    uint16_t rec_len;       // Directory entry length (could be sizeof(dir_entry))
    uint8_t  name_len;      // Length of 'name'
    uint8_t  file_type;     // e.g., 0=regular, 1=directory, etc. (ext4 uses DT_* macros)
    char     name[MAX_FILENAME_LEN + 1]; // +1 for null terminator
} dir_entry_t;

typedef struct directory_block {
    uint32_t entries_count;
    dir_entry_t entries[];  
} directory_block_t;

directory_block_t *allocate_directory_block(size_t num_entries) {
    // Calculate total bytes: the struct plus num_entries of dir_entry_t
    size_t block_size = sizeof(directory_block_t) + num_entries * sizeof(dir_entry_t);

    // Allocate as a single memory region
    directory_block_t *dirblk = (directory_block_t *)malloc(block_size);
    if (!dirblk) {
        return NULL;
    }
    // Initialize
    dirblk->entries_count = (uint32_t)num_entries;
    memset(dirblk->entries, 0, num_entries * sizeof(dir_entry_t));
    return dirblk;
}

// A small helper to create a minimal directory block with "." and ".."
directory_block_t* create_minimal_directory_block(uint32_t self_inode, uint32_t parent_inode) {
    // We'll store exactly 2 entries
    size_t count = 2;
    size_t total_size = sizeof(directory_block_t) + count * sizeof(dir_entry_t);

    directory_block_t *dirblk = (directory_block_t *)malloc(total_size);
    if (!dirblk) return NULL;

    dirblk->entries_count = count;
    memset(dirblk->entries, 0, count * sizeof(dir_entry_t));

    // "." entry
    dirblk->entries[0].inode    = self_inode;
    dirblk->entries[0].rec_len  = sizeof(dir_entry_t);
    dirblk->entries[0].name_len = 1;
    dirblk->entries[0].file_type = 2;  // Directory
    strcpy(dirblk->entries[0].name, ".");

    // ".." entry
    dirblk->entries[1].inode    = parent_inode;
    dirblk->entries[1].rec_len  = sizeof(dir_entry_t);
    dirblk->entries[1].name_len = 2;
    dirblk->entries[1].file_type = 2; // Directory
    strcpy(dirblk->entries[1].name, "..");

    return dirblk;
}


#endif // FILE_H