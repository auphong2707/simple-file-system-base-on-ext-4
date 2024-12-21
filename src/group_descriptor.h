#ifndef GROUP_DESCRIPTOR_H
#define GROUP_DESCRIPTOR_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef struct group_descriptor {
    uint32_t block_bitmap;      // Block number of the block bitmap.
                                // The block bitmap tracks which blocks in the block group are free or allocated.

    uint32_t inode_bitmap;      // Block number of the inode bitmap.
                                // The inode bitmap tracks which inodes in the block group are free or allocated.

    uint32_t inode_table;       // Block number of the inode table.
                                // The inode table stores metadata for all inodes in the block group.

    uint32_t free_blocks_count; // Total number of free blocks in this block group.
                                // Helps the file system track available space within the group.

    uint32_t free_inodes_count; // Total number of free inodes in this block group.
                                // Indicates how many inodes are available for new files or directories.

    uint32_t used_dirs_count;   // Total number of in-use directory inodes in this block group.
                                // This is useful for optimizing directory-related operations.
} group_descriptor;

void initialize_descriptor_block(struct group_descriptor *db, uint32_t block_bitmap, uint32_t inode_bitmap, uint32_t inode_table, uint32_t free_blocks_count, uint32_t free_inodes_count, uint32_t used_dirs_count) {
    db->block_bitmap = block_bitmap;
    db->inode_bitmap = inode_bitmap;
    db->inode_table = inode_table;
    db->free_blocks_count = free_blocks_count;
    db->free_inodes_count = free_inodes_count;
    db->used_dirs_count = used_dirs_count;
}

void print_descriptor_block(const struct group_descriptor *db) {
    printf("Descriptor Block Information:\n");
    printf("Block Bitmap Address   : %u\n", db->block_bitmap);
    printf("Inode Bitmap Address   : %u\n", db->inode_bitmap);
    printf("Inode Table Address    : %u\n", db->inode_table);
    printf("Free Blocks Count      : %u\n", db->free_blocks_count);
    printf("Free Inodes Count      : %u\n", db->free_inodes_count);
    printf("Used Directories Count : %u\n", db->used_dirs_count);
}

#endif