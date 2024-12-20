#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define BLOCK_COUNT  // Maximum number of blocks in a block group

// Initialize the bitmap (set all bits to 0)
void initialize_bitmap(uint8_t *bitmap, int block_count) {
    int byte_count = (block_count + 7) / 8; // Calculate required number of bytes
    for (int i = 0; i < byte_count; i++) {
        bitmap[i] = 0; // Set all bytes to 0
    }
}

// Check if a block is free
bool is_block_free(uint8_t *bitmap, int block_index) {
    return !(bitmap[block_index / 8] & (1 << (block_index % 8)));
}

// Mark a block as allocated
void allocate_block(uint8_t *bitmap, int block_index) {
    bitmap[block_index / 8] |= (1 << (block_index % 8));
}

// Free a block (set it back to 0)
void free_block(uint8_t *bitmap, int block_index) {
    bitmap[block_index / 8] &= ~(1 << (block_index % 8));
}

// Display the current state of the bitmap (for debugging purposes)
void print_bitmap(uint8_t *bitmap, int block_count) {
    for (int i = 0; i < block_count; i++) {
        printf("%d", is_block_free(bitmap, i) ? 0 : 1);
    }
    printf("\n");
}

// int main() {
//     uint8_t block_bitmap[(BLOCK_COUNT + 7) / 8]; // Declare the bitmap

//     // Initialize the bitmap
//     initialize_bitmap(block_bitmap, BLOCK_COUNT);
//     printf("Initial bitmap: ");
//     print_bitmap(block_bitmap, BLOCK_COUNT);

//     // Allocate blocks 0, 1, and 2
//     allocate_block(block_bitmap, 0);
//     allocate_block(block_bitmap, 1);
//     allocate_block(block_bitmap, 2);
//     printf("After allocation: ");
//     print_bitmap(block_bitmap, BLOCK_COUNT);

//     // Free block 1
//     free_block(block_bitmap, 1);
//     printf("After freeing block 1: ");
//     print_bitmap(block_bitmap, BLOCK_COUNT);

//     return 0;
// }
