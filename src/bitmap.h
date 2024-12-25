#ifndef BITMAP_H
#define BITMAP_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// Initialize the bitmap (set all bits to 0)
void initialize_bitmap(uint8_t *bitmap, int block_count) {
    int byte_count = (block_count + 7) / 8; // Calculate required number of bytes
    for (int i = 0; i < byte_count; i++) {
        bitmap[i] = 0; // Set all bytes to 0
    }
}

// Check if a block is free
bool is_bit_free(uint8_t *bitmap, int block_index) {
    return !(bitmap[block_index / 8] & (1 << (block_index % 8)));
}

// Mark a block as allocated
void set_bitmap_bit(uint8_t *bitmap, int block_index) {
    bitmap[block_index / 8] |= (1 << (block_index % 8));
}

// Free a block (set it back to 0)
void free_bitmap_bit(uint8_t *bitmap, int block_index) {
    bitmap[block_index / 8] &= ~(1 << (block_index % 8));
}

// Find a free block
int find_free_block(uint8_t *bitmap, int block_count, int start_from) {
    for (int i = start_from; i < block_count; i++) {
        if (is_bit_free(bitmap, i)) {
            return i;
        }
    }
    return -1;
}

// Display the current state of the bitmap (for debugging purposes)
void print_bitmap(uint8_t *bitmap, int block_count) {
    printf("Allocated blocks: ");
    for (int i = 0; i < block_count; i++) {
        if (!is_bit_free(bitmap, i)) {
            printf("%d ", i);
        }
    }
    printf("\n");
}

#endif