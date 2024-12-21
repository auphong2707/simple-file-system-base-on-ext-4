
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "superblock.h"
#include "group_descriptor.h"

# define FILENAME "drive.bin"
# define BLOCK_SIZE 4096
# define INODE_SIZE 128
# define BLOCKS_COUNT 32768
# define INODES_COUNT 8192
# define BLOCKS_PER_GROUP 16384
# define INODES_PER_GROUP 4096

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

// Initialize the drive with the superblock, group descriptors, and other structures
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
    struct group_descriptor gd;
    initialize_descriptor_block(&gd, 2, 3, 4, BLOCKS_PER_GROUP - 5, INODES_PER_GROUP, 0);
    fseek(file, BLOCK_SIZE, SEEK_SET);
    fwrite(&gd, sizeof(struct group_descriptor), 1, file);

    // Write the Data Block Bitmap

    // Write the inode Bitmap

    // Write the inode Table

    fclose(file);
}

int main() {
    create_drive_file(FILENAME, BLOCK_SIZE * BLOCKS_COUNT);
    initialize_drive(FILENAME);
}