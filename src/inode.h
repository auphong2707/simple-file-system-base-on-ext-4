#ifndef INODE_H
#define INODE_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "bitmap.h"


# define INODES_COUNT 8192
# define INODES_PER_GROUP 4096

// Define the inode structure
typedef struct inode {
    uint32_t inode_number;       // Unique identifier for the inode
    uint32_t file_size;          // Size of the file in bytes
    uint32_t blocks[12];         // Direct block pointers (12 direct blocks)
    uint32_t single_indirect;    // Pointer to a single indirect block
    uint32_t double_indirect;    // Pointer to a double indirect block
    uint32_t file_type;          // Type of file (e.g., 0 = regular, 1 = directory)
    uint32_t permissions;        // Permissions (e.g., rwxrwxrwx as a bitmask)
} inode;

# define INODE_SIZE sizeof(inode)

// Define the inode table
typedef struct inode_table {
    inode inodes[INODES_COUNT]; // Array of inodes
    uint32_t used_inodes;            // Number of inodes currently in use
} inode_table;

// Function to initialize an inode
void initialize_inode(inode *node, uint32_t inode_number, uint32_t file_type, uint32_t permissions) {
    node->inode_number = inode_number;
    node->file_size = 0; // Initially, the file size is 0
    memset(node->blocks, 0, sizeof(node->blocks)); // Initialize all block pointers to 0
    node->single_indirect = 0; // Initialize single indirect pointer to 0
    node->double_indirect = 0; // Initialize double indirect pointer to 0
    node->file_type = file_type; // Set the file type
    node->permissions = permissions; // Set file permissions
}

// Function to initialize the inode table
void initialize_inode_table(inode_table *inode_table) {
    inode_table->used_inodes = 0;
    for (int i = 0; i < INODES_COUNT; i++) {
        initialize_inode(&inode_table->inodes[i], 0, 0, 0); // Initialize all inodes with default values
    }
}

// Function to allocate a new inode in the table
inode *allocate_inode(inode_table *inode_table, uint8_t *inode_bitmap, uint32_t file_type, uint32_t permissions) {
    for (uint32_t i = 0; i < INODES_COUNT; i++) {
        if (is_bit_free(inode_bitmap, i)) {
            inode *new_inode = &inode_table->inodes[i];
            initialize_inode(new_inode, i + 1, file_type, permissions);
            set_bitmap_bit(inode_bitmap, i);
            inode_table->used_inodes++;
            return new_inode;
        }
    }
    printf("Error: Inode table is full.\n");
    return NULL;
}

// Function to deallocate an inode
void deallocate_inode(inode_table *inode_table, uint8_t *inode_bitmap, uint32_t inode_number) {
    if (inode_number == 0 || inode_number > INODES_COUNT) {
        printf("Error: Invalid inode number.\n");
        return;
    }
    uint32_t index = inode_number - 1;
    if (!is_bit_free(inode_bitmap, index)) {
        free_bitmap_bit(inode_bitmap, index);
        memset(&inode_table->inodes[index], 0, sizeof(inode)); // Clear the inode
        inode_table->used_inodes--;
    } else {
        printf("Error: Inode %u is not allocated.\n", inode_number);
    }
}

// Function to print all inodes in the table
void print_inode_table(const inode_table *inode_table, uint8_t *inode_bitmap) {
    printf("Inode Table:\n");
    for (uint32_t i = 0; i < INODES_COUNT; i++) {
        if (!is_bit_free(inode_bitmap, i)) {
            const inode *node = &inode_table->inodes[i];
            printf("Inode Number: %u\n", node->inode_number);
            printf("  File Size: %u bytes\n", node->file_size);
            printf("  File Type: %s\n", (node->file_type == 0) ? "Regular File" : "Directory");
            printf("  Permissions: %o\n", node->permissions);
            printf("  Direct Blocks: ");
            for (int j = 0; j < 12; j++) {
                printf("%u ", node->blocks[j]);
            }
            printf("\n  Single Indirect Block: %u\n", node->single_indirect);
            printf("  Double Indirect Block: %u\n\n", node->double_indirect);
        }
    }
}

#endif