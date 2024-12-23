
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "superblock.h"
#include "group_descriptor.h"
#include "bitmap.h"
#include "inode.h"
#include "file.h"

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

// Create a file by allocating an inode and required data blocks
void create_file(FILE *file, uint8_t *inode_bitmap, int *inode_table, uint8_t *block_bitmap, size_t block_count, file_t *file_info) {
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

    size_t data_length = file_info->size;
    size_t blocks_needed = (data_length + BLOCK_SIZE - 1) / BLOCK_SIZE; // Round up to nearest block
    int *allocated_blocks = (int *)malloc(blocks_needed * sizeof(int));
    if (allocated_blocks == NULL) {
        fprintf(stderr, "Error: Memory allocation failed.\n");
        return;
    }

    // Find free blocks
    size_t allocated_count = 0;
    for (size_t i = 0; i < block_count && allocated_count < blocks_needed; i++) {
        if (!is_used(block_bitmap, i)) {
            allocated_blocks[allocated_count++] = i;
            set_used(block_bitmap, i);
        }
    }

    if (allocated_count < blocks_needed) {
        fprintf(stderr, "Error: Not enough free blocks available.\n");
        for (size_t i = 0; i < allocated_count; i++) {
            set_free(block_bitmap, allocated_blocks[i]);
        }
        set_free(inode_bitmap, inode_index);
        free(allocated_blocks);
        return;
    }

    // Assign the first block to the inode
    inode_table[inode_index] = allocated_blocks[0];

    // Write file metadata to the allocated block
    fseek(file, allocated_blocks[0] * BLOCK_SIZE, SEEK_SET);
    fwrite(file_info, sizeof(file_t), 1, file);

    // Write file data to remaining allocated blocks
    size_t bytes_written = 0;
    for (size_t i = 1; i < blocks_needed; i++) {
        fseek(file, allocated_blocks[i] * BLOCK_SIZE, SEEK_SET);
        size_t bytes_to_write = (data_length - bytes_written > BLOCK_SIZE) ? BLOCK_SIZE : (data_length - bytes_written);
        fwrite(((char *)file_info) + sizeof(file_t) + bytes_written, 1, bytes_to_write, file);
        bytes_written += bytes_to_write;
    }

    printf("File created with inode %d and %zu blocks.\n", inode_index, blocks_needed);
    printf("Metadata written for file: %s.%s\n", file_info->name, file_info->extension);

    free(allocated_blocks);
}

// Delete a file by freeing its inode and data block
void delete_file(FILE *file, uint8_t *inode_bitmap, int *inode_table, uint8_t *block_bitmap, int inode_index) {
    if (inode_index < 0 || inode_index >= MAX_INODE_COUNT || !is_used(inode_bitmap, inode_index)) {
        fprintf(stderr, "Error: Invalid inode index or inode not in use.\n");
        return;
    }

    // Free the block associated with the inode
    int block_index = inode_table[inode_index];
    if (block_index >= 0 && block_index < BLOCKS_COUNT) {
        set_free(block_bitmap, block_index);
    }

    // Free the inode
    set_free(inode_bitmap, inode_index);
    inode_table[inode_index] = -1;

    printf("File with inode %d deleted successfully.\n", inode_index);
}

int main() {
    create_drive_file(FILENAME, BLOCK_SIZE * BLOCKS_COUNT);
    initialize_drive(FILENAME);
}