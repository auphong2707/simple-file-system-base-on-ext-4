#ifndef FILE_H
#define FILE_H

#include <stdint.h>
#include <time.h>

#define MAX_FILENAME_LEN 255

// Define the file structure
typedef struct {
    char name[256];      // File name
    char extension[16];  // File extension
    uint64_t size;       // File size in bytes
    uint32_t inode;      // Inode number
    char data[];         // Flexible array for variable-sized data
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
    dirblk->entries[0].file_type = 1;  // Directory
    strcpy(dirblk->entries[0].name, ".");

    // ".." entry
    dirblk->entries[1].inode    = parent_inode;
    dirblk->entries[1].rec_len  = sizeof(dir_entry_t);
    dirblk->entries[1].name_len = 2;
    dirblk->entries[1].file_type = 1; // Directory
    strcpy(dirblk->entries[1].name, "..");

    return dirblk;
}

directory_block_t *add_entry_to_directory_block(directory_block_t *dirblk, uint32_t inode, const char *name, uint8_t file_type) {
    // Initialize the new directory block
    size_t new_size = sizeof(directory_block_t) + (dirblk->entries_count + 1) * sizeof(dir_entry_t);

    directory_block_t *new_dirblk = (directory_block_t *)malloc(new_size);
    if (!new_dirblk) {
        return NULL;
    }

    // Copy the existing entries
    memcpy(new_dirblk, dirblk, sizeof(directory_block_t) + dirblk->entries_count * sizeof(dir_entry_t));

    // Add the new entry
    dir_entry_t *new_entry = &new_dirblk->entries[dirblk->entries_count];
    new_entry->inode = inode;
    new_entry->rec_len = sizeof(dir_entry_t);
    new_entry->name_len = (uint8_t)strlen(name);
    new_entry->file_type = file_type;
    strncpy(new_entry->name, name, MAX_FILENAME_LEN);

    // Update the entries count
    new_dirblk->entries_count++;

    return new_dirblk;
}

directory_block_t *remove_entry_from_directory_block(directory_block_t *dirblk, uint32_t inode) {
    // Find the entry to remove
    size_t i;
    for (i = 0; i < dirblk->entries_count; i++) {
        if (dirblk->entries[i].inode == inode) {
            break;
        }
    }

    // If the entry was not found
    if (i == dirblk->entries_count) {
        return NULL;
    }

    // Calculate the new size
    size_t new_size = sizeof(directory_block_t) + (dirblk->entries_count - 1) * sizeof(dir_entry_t);

    // Allocate the new directory block
    directory_block_t *new_dirblk = (directory_block_t *)malloc(new_size);
    if (!new_dirblk) {
        return NULL;
    }

    for (size_t j = 0; j < i; j++) {
        new_dirblk->entries[j] = dirblk->entries[j];
    }

    for (size_t j = i + 1; j < dirblk->entries_count; j++) {
        new_dirblk->entries[j - 1] = dirblk->entries[j];
    }

    // Update the entries count
    new_dirblk->entries_count = dirblk->entries_count - 1;

    return new_dirblk;
}

#endif // FILE_H
