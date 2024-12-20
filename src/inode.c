#include <stdint.h>
#include <stdio.h>

// Define a simplified inode structure
typedef struct inode {
    uint16_t i_mode;        // File mode
    uint16_t i_uid;         // Owner UID
    uint32_t i_size;        // File size in bytes
    uint32_t i_atime;       // Last access time
    uint32_t i_ctime;       // Last inode change time
    uint32_t i_mtime;       // Last data modification time
    uint16_t i_gid;         // Group ID
    uint16_t i_links_count; // Hard link count
    uint32_t i_blocks;      // Number of 512-byte blocks allocated
    uint32_t i_block[15];   // Pointers to blocks (simplified to direct blocks)
} inode;

void print_inode(inode *node) {
    printf("File Mode: %u\n", node->i_mode);
    printf("Owner UID: %u\n", node->i_uid);
    printf("File Size: %u bytes\n", node->i_size);
    printf("Last Access Time: %u\n", node->i_atime);
    printf("Last Change Time: %u\n", node->i_ctime);
    printf("Last Modification Time: %u\n", node->i_mtime);
    printf("Group ID: %u\n", node->i_gid);
    printf("Links Count: %u\n", node->i_links_count);
    printf("Blocks Count: %u\n", node->i_blocks);
    for (int i = 0; i < 15; i++) {
        printf("Block[%d]: %u\n", i, node->i_block[i]);
    }
}

int main() {
    // Create a sample inode instance
    inode sample_inode = {
        .i_mode = 0x81A4,         // Regular file with 644 permissions
        .i_uid = 1000,            // Example UID
        .i_size = 4096,           // File size in bytes
        .i_atime = 1672531200,    // Example access time (epoch seconds)
        .i_ctime = 1672531300,    // Example change time
        .i_mtime = 1672531400,    // Example modification time
        .i_gid = 1000,            // Example GID
        .i_links_count = 1,       // Single hard link
        .i_blocks = 8,            // Number of blocks (example)
        .i_block = {0}            // Initialize block pointers to zero
    };

    // Print the inode details
    print_inode(&sample_inode);

    return 0;
}
