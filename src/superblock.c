#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Định nghĩa cấu trúc superblock
struct superblock {
    uint32_t total_blocks;      // Tổng số block trong hệ thống file
    uint32_t total_inodes;      // Tổng số inode
    uint32_t block_size;        // Kích thước block (bytes)
    uint32_t inode_size;        // Kích thước inode (bytes)
    uint32_t blocks_per_group;  // Số block mỗi group
    uint32_t inodes_per_group;  // Số inode mỗi group
    uint32_t first_data_block;  // Vị trí block dữ liệu đầu tiên
    char fs_uuid[16];           // UUID của hệ thống file
    char volume_name[32];       // Tên hệ thống file
    uint32_t magic_number;      // Magic number để nhận diện hệ thống file
};

// Hàm khởi tạo superblock
void initialize_superblock(struct superblock *sb, uint32_t total_blocks, uint32_t total_inodes, uint32_t block_size, uint32_t inode_size) {
    sb->total_blocks = total_blocks;
    sb->total_inodes = total_inodes;
    sb->block_size = block_size;
    sb->inode_size = inode_size;
    sb->blocks_per_group = 128; // Ví dụ: mỗi group có 128 block
    sb->inodes_per_group = 32;  // Ví dụ: mỗi group có 32 inode
    sb->first_data_block = 1;   // Block dữ liệu đầu tiên (0 thường dành cho superblock)

    // Gán UUID và tên volume
    strncpy(sb->fs_uuid, "123456789abcdef", 16);
    strncpy(sb->volume_name, "MyFileSystem", 32);

    // Magic number để nhận diện hệ thống file (ví dụ: 0xEF53 cho Ext4)
    sb->magic_number = 0xEF53;
}

// Hàm hiển thị nội dung superblock
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

//     // Khởi tạo superblock với thông số ví dụ
//     initialize_superblock(&sb, 1024, 128, 4096, 128);

//     // In nội dung superblock
//     print_superblock(&sb);

//     return 0;
// }