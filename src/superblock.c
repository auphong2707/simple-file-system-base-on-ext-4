#include <stdio.h>
#include <stdint.h>
#include <string.h>

struct superblock {
    uint32_t total_blocks;      // Total number of blocks in the file system.
                                // This defines the total storage capacity in terms of blocks.

    uint32_t total_inodes;      // Total number of inodes in the file system.
                                // Inodes represent metadata for files and directories.

    uint32_t block_size;        // Size of each block in bytes.
                                // This determines how much data each block can store, e.g., 4096 bytes (4KB).

    uint32_t inode_size;        // Size of each inode in bytes.
                                // This defines how much metadata each inode can hold.

    uint32_t blocks_per_group;  // Number of blocks in each block group.
                                // The file system is divided into block groups for better management and locality.

    uint32_t inodes_per_group;  // Number of inodes in each block group.
                                // Specifies how many inodes are available per block group.

    uint32_t first_data_block;  // Index of the first data block.
                                // This typically skips reserved blocks, like the superblock and group descriptors.

    char fs_uuid[16];           // A unique identifier (UUID) for the file system.
                                // Helps distinguish between file systems, even on similar storage devices.

    char volume_name[32];       // The name of the file system volume.
                                // Provides a human-readable name for the file system.

    uint32_t magic_number;      // A constant value to identify the file system type.
                                // For example, 0xEF53 is commonly used for Ext4.
};

void initialize_superblock(
        struct superblock *sb, 
        uint32_t total_blocks, 
        uint32_t total_inodes, 
        uint32_t block_size, 
        uint32_t inode_size,
        uint32_t blocks_per_group,
        uint32_t inodes_per_group,
        uint32_t first_data_block,
        const char *fs_uuid,
        const char *volume_name,
        uint32_t magic_number
    ) 
{
    sb->total_blocks = total_blocks;
    sb->total_inodes = total_inodes;
    sb->block_size = block_size;
    sb->inode_size = inode_size;
    sb->blocks_per_group = blocks_per_group;
    sb->inodes_per_group = inodes_per_group;
    sb->first_data_block = first_data_block;
    strncpy(sb->fs_uuid, fs_uuid, sizeof(sb->fs_uuid) - 1);
    strncpy(sb->volume_name, volume_name, sizeof(sb->volume_name) - 1);
    sb->magic_number = magic_number;
}

void print_superblock(const struct superblock *sb) {
    printf("Superblock Information:\n");
    printf("Total Blocks       : %u\n", sb->total_blocks);
    printf("Total Inodes       : %u\n", sb->total_inodes);
    printf("Block Size         : %u bytes\n", sb->block_size);
    printf("Inode Size         : %u bytes\n", sb->inode_size);
    printf("Blocks Per Group   : %u\n", sb->blocks_per_group);
    printf("Inodes Per Group   : %u\n", sb->inodes_per_group);
    printf("First Data Block   : %u\n", sb->first_data_block);
    printf("File System UUID   : %s\n", sb->fs_uuid);
    printf("Volume Name        : %s\n", sb->volume_name);
    printf("Magic Number       : 0x%X\n", sb->magic_number);
}

// int main() {
//     struct superblock sb;

//     initialize_superblock(&sb, 1024, 128, 4096, 128);

//     print_superblock(&sb);

//     return 0;
// }
