#include <stdio.h>
#include <stdint.h>
#include <string.h>

struct superblock {
    uint32_t total_blocks;
    uint32_t total_inodes;
    uint32_t block_size;
    uint32_t inode_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t first_data_block;
    char fs_uuid[16];
    char volume_name[32];
    uint32_t magic_number;
};

void initialize_superblock(struct superblock *sb, uint32_t total_blocks, uint32_t total_inodes, uint32_t block_size, uint32_t inode_size) {
    sb->total_blocks = total_blocks;
    sb->total_inodes = total_inodes;
    sb->block_size = block_size;
    sb->inode_size = inode_size;
    sb->blocks_per_group = 128;
    sb->inodes_per_group = 32;
    sb->first_data_block = 1;
    strncpy(sb->fs_uuid, "123456789abcdef", 16);
    strncpy(sb->volume_name, "MyFileSystem", 32);
    sb->magic_number = 0xEF53;
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
