#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define MAX_INODES 100 // Maximum number of inodes in the table

// Define the inode structure
struct inode {
    uint32_t inode_number;       // Unique identifier for the inode
    uint32_t file_size;          // Size of the file in bytes
    uint32_t blocks[12];         // Direct block pointers (12 direct blocks)
    uint32_t single_indirect;    // Pointer to a single indirect block
    uint32_t double_indirect;    // Pointer to a double indirect block
    uint32_t file_type;          // Type of file (e.g., 0 = regular, 1 = directory)
    uint32_t permissions;        // Permissions (e.g., rwxrwxrwx as a bitmask)
};

// Define the inode table
struct inode_table {
    struct inode inodes[MAX_INODES]; // Array of inodes
    uint32_t used_inodes;            // Number of inodes currently in use
};

// Function to initialize an inode
void initialize_inode(struct inode *node, uint32_t inode_number, uint32_t file_type, uint32_t permissions) {
    node->inode_number = inode_number;
    node->file_size = 0; // Initially, the file size is 0
    memset(node->blocks, 0, sizeof(node->blocks)); // Initialize all block pointers to 0
    node->single_indirect = 0; // Initialize single indirect pointer to 0
    node->double_indirect = 0; // Initialize double indirect pointer to 0
    node->file_type = file_type; // Set the file type
    node->permissions = permissions; // Set file permissions
}

// Function to initialize the inode table
void initialize_inode_table(struct inode_table *table) {
    table->used_inodes = 0;
    for (int i = 0; i < MAX_INODES; i++) {
        initialize_inode(&table->inodes[i], 0, 0, 0); // Initialize all inodes with default values
    }
}

// Function to allocate a new inode in the table
struct inode *allocate_inode(struct inode_table *table, uint32_t file_type, uint32_t permissions) {
    if (table->used_inodes >= MAX_INODES) {
        printf("Error: Inode table is full.\n");
        return NULL;
    }
    struct inode *new_inode = &table->inodes[table->used_inodes];
    initialize_inode(new_inode, table->used_inodes + 1, file_type, permissions);
    table->used_inodes++;
    return new_inode;
}

// Function to print all inodes in the table
void print_inode_table(const struct inode_table *table) {
    printf("Inode Table:\n");
    for (uint32_t i = 0; i < table->used_inodes; i++) {
        const struct inode *node = &table->inodes[i];
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

// Example usage
int main() {
    struct inode_table my_table;
    initialize_inode_table(&my_table);

    // Allocate some inodes
    allocate_inode(&my_table, 0, 0755); // Regular file with rwxr-xr-x permissions
    allocate_inode(&my_table, 1, 0700); // Directory with rwx------ permissions

    // Print the inode table
    print_inode_table(&my_table);

    return 0;
}
