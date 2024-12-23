
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "superblock.h"
#include "group_descriptor.h"
#include "bitmap.h"
#include "inode.h"

# define FILENAME "drive.bin"
# define BLOCK_SIZE 4096
# define BLOCKS_COUNT 32768
# define BLOCKS_PER_GROUP 16384
# define MAX_INODE_COUNT 1024

// Create a file with the specified size
void create_drive_file(const char *filename, uint64_t size) {
    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        fprintf(stderr, "Error: Unable to create file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    fseek(file, size - 1, SEEK_SET); // Move the file pointer to the last byte
    fputc('\0', file); // Write a null byte

    fclose(file);
}

// Initialize the drive with the superblock, group descriptors, and other structures:
// 1. Super Block: 1 block
// 2. Group Descriptor: 1 block
// 3. Data Block Bitmap: 1 block
// 4. Inode Bitmap: 1 block
// 5. Inode Table: Many blocks
// 6. Data Blocks: Remaining blocks
void initialize_drive(const char *filename) {
    FILE *file = fopen(filename, "rb+");
    if (file == NULL) {
        fprintf(stderr, "Error: Unable to open file %s\n", filename);
        exit(EXIT_FAILURE);
    }
    
    // Write the Super Block to the First Block
    superblock sb;
    initialize_superblock(&sb, BLOCKS_COUNT, INODES_COUNT, BLOCK_SIZE, INODE_SIZE, BLOCKS_PER_GROUP, INODES_PER_GROUP, 2, "1234567890abcdef", "MyDrive", 0xEF53);
    fseek(file, 0, SEEK_SET);
    fwrite(&sb, sizeof(superblock), 1, file);

    // Write the Group Descriptor to the Second Block
    group_descriptor gd;
    initialize_descriptor_block(&gd, 2, 3, 4, BLOCKS_PER_GROUP - 5, INODES_PER_GROUP, 0);
    fseek(file, BLOCK_SIZE, SEEK_SET);
    fwrite(&gd, sizeof(struct group_descriptor), 1, file);

    // Write the Data Block Bitmap
    uint8_t *data_block_bitmap = (uint8_t *) malloc(BLOCKS_COUNT / 8);
    initialize_bitmap(data_block_bitmap, BLOCKS_COUNT);
    fseek(file, 2 * BLOCK_SIZE, SEEK_SET);
    fwrite(data_block_bitmap, BLOCKS_COUNT / 8, 1, file);

    // Write the inode Bitmap
    uint8_t *inode_bitmap = (uint8_t *) malloc(INODES_COUNT / 8);
    initialize_bitmap(inode_bitmap, INODES_COUNT);
    fseek(file, 3 * BLOCK_SIZE, SEEK_SET);
    fwrite(inode_bitmap, INODES_COUNT / 8, 1, file);

    // Write the inode Table
    inode_table inode_table;
    initialize_inode_table(&inode_table);
    fseek(file, 4 * BLOCK_SIZE, SEEK_SET);
    fwrite(&inode_table, sizeof(inode_table), 1, file);

    fclose(file);
}

// Create a file by allocating an inode and data block
void create_file(FILE *file, uint8_t *inode_bitmap, int *inode_table, uint8_t *block_bitmap, size_t block_count, const char *data) {
    // Find a free inode
    int inode_index = -1;
    for (size_t i = 0; i < MAX_INODE_COUNT; i++) {
        if (!is_used(inode_bitmap, i)) {
            inode_index = i;
            set_used(inode_bitmap, i);
            break;
        }
    }

    if (inode_index == -1) {
        fprintf(stderr, "Error: No free inodes available.\n");
        return;
    }

    // Find a free block
    int block_index = -1;
    for (size_t i = 0; i < block_count; i++) {
        if (!is_used(block_bitmap, i)) {
            block_index = i;
            set_used(block_bitmap, i);
            break;
        }
    }

    if (block_index == -1) {
        fprintf(stderr, "Error: No free blocks available.\n");
        set_free(inode_bitmap, inode_index); // Free the allocated inode
        return;
    }

    // Assign the block to the inode
    inode_table[inode_index] = block_index;

    // Simulate writing data to the block (this would normally involve file I/O)
    printf("File created with inode %d and block %d.\n", inode_index, block_index);
    printf("Data: %s\n", data);
}

int main() {
    create_drive_file(FILENAME, BLOCK_SIZE * BLOCKS_COUNT);
    initialize_drive(FILENAME);
}