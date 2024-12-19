#include <stdio.h>
#include <stdint.h>
#include <string.h>

struct descriptor_block {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint32_t free_blocks_count;
    uint32_t free_inodes_count;
    uint32_t used_dirs_count;
};

void initialize_descriptor_block(struct descriptor_block *db, uint32_t block_bitmap, uint32_t inode_bitmap, uint32_t inode_table, uint32_t free_blocks_count, uint32_t free_inodes_count, uint32_t used_dirs_count) {
    db->block_bitmap = block_bitmap;
    db->inode_bitmap = inode_bitmap;
    db->inode_table = inode_table;
    db->free_blocks_count = free_blocks_count;
    db->free_inodes_count = free_inodes_count;
    db->used_dirs_count = used_dirs_count;
}

void print_descriptor_block(const struct descriptor_block *db) {
    printf("Descriptor Block Information:\n");
    printf("Block Bitmap Address   : %u\n", db->block_bitmap);
    printf("Inode Bitmap Address   : %u\n", db->inode_bitmap);
    printf("Inode Table Address    : %u\n", db->inode_table);
    printf("Free Blocks Count      : %u\n", db->free_blocks_count);
    printf("Free Inodes Count      : %u\n", db->free_inodes_count);
    printf("Used Directories Count : %u\n", db->used_dirs_count);
}

// int main() {
//     struct descriptor_block db;

//     initialize_descriptor_block(&db, 5, 6, 7, 100, 50, 10);

//     print_descriptor_block(&db);

//     return 0;
// }
