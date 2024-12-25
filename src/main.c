
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "superblock.h"
#include "group_descriptor.h"
#include "bitmap.h"
#include "inode.h"
#include "file.h"

# define FILENAME "drive.bin"
# define BLOCK_SIZE 4096
# define BLOCKS_COUNT 32768
# define BLOCKS_PER_GROUP 16384
# define MAX_INODE_COUNT 1024
# define FIRST_DATA_BLOCK (4 + INODES_COUNT * INODE_SIZE / BLOCK_SIZE + 1)


// [HELPER FUNCTIONS]
// Find a free block and allocate it
int find_and_allocate_free_block(uint8_t *block_bitmap, group_descriptor *gd) {
    for (int i = 0; i < BLOCKS_COUNT; i++) {
        if (!is_bit_set(block_bitmap, i)) {
            set_bit(block_bitmap, i);
            gd->free_blocks_count--;
            return i;
        }
    }
    return -1;
}

// Read a block reference from the disk
int read_block_reference(FILE *disk, uint32_t block_index, uint32_t entry_index, uint32_t *out_block_num) {
    // Seek to: block_index * BLOCK_SIZE + entry_index * 4
    if (fseek(disk, (long)block_index * BLOCK_SIZE + entry_index * sizeof(uint32_t), SEEK_SET) != 0) {
        return -1;
    }
    if (fread(out_block_num, sizeof(uint32_t), 1, disk) != 1) {
        return -1;
    }
    return 0;
}

// Write a block reference to the disk
int write_block_reference(FILE *disk, uint32_t block_index, uint32_t entry_index, uint32_t block_num) {
    if (fseek(disk, (long)block_index * BLOCK_SIZE + entry_index * sizeof(uint32_t), SEEK_SET) != 0) {
        return -1;
    }
    if (fwrite(&block_num, sizeof(uint32_t), 1, disk) != 1) {
        return -1;
    }
    return 0;
}

// Zero out a block on the disk
void zero_block_on_disk(FILE *disk, uint32_t block_index) {
    static uint8_t zero_buf[BLOCK_SIZE];
    memset(zero_buf, 0, BLOCK_SIZE);
    fseek(disk, (long)block_index * BLOCK_SIZE, SEEK_SET);
    fwrite(zero_buf, BLOCK_SIZE, 1, disk);
}

/**
 * Allocate a new data block for the 'n'-th (0-based) block of this inode.
 * 
 * Handling:
 *  - If n < 12, uses direct blocks.
 *  - If 12 <= n < 12 + 1024, uses single_indirect.
 *  - If 12 + 1024 <= n < 12 + 1024 + (1024*1024), uses double_indirect.
 *    (Ignoring triple-indirect for simplicity.)
 * 
 *  Each indirect block is an array of 1024 uint32_t block references.
 * 
 * Returns: the newly allocated block index on success, or -1 on failure.
 */
int allocate_data_block_for_inode(
    FILE *disk,
    inode *node,
    uint32_t n,
    uint8_t *block_bitmap,
    group_descriptor *gd
) {
    // Step 1: find a free data block in the bitmap and allocate it
    int new_data_block = find_and_allocate_free_block(block_bitmap, gd);
    if (new_data_block == -1) {
        fprintf(stderr, "Error: No free data blocks available.\n");
        return -1;
    }

    zero_block_on_disk(disk, (uint32_t)new_data_block);

    // Step 2: figure out where to store 'new_data_block' in the inode
    // Step 2a: Direct blocks (0..11)
    if (n < 12) {
        node->blocks[n] = new_data_block;
        return new_data_block;
    }

    // Step 2b: Single indirect range (12..12+1024-1)
    uint32_t single_start = 12;
    uint32_t single_end = single_start + 1024 - 1; // up to 12 + 1024 - 1 = 1035

    if (n <= single_end) {
        uint32_t si_offset = n - single_start;

        // If single_indirect == 0, allocate the single-indirect block itself
        if (node->single_indirect == 0) {
            int si_block = find_and_allocate_free_block(block_bitmap, gd);
            if (si_block == -1) {
                fprintf(stderr, "Error: No free blocks for single indirect block.\n");
                // rollback
                free_bitmap_bit(block_bitmap, new_data_block);
                gd -> free_blocks_count++;
                return -1;
            }
            node->single_indirect = si_block;
            zero_block_on_disk(disk, (uint32_t)si_block);
        }

        // Write 'new_data_block' to the single indirect block
        if (write_block_reference(disk, node->single_indirect, si_offset, (uint32_t)new_data_block) != 0) {
            fprintf(stderr, "Error: Could not write single_indirect reference.\n");
            // roll back
            free_bitmap_bit(block_bitmap, new_data_block);
            gd->free_blocks_count++;
            return -1;
        }

        return new_data_block;
    }
    
    // Double indirect range: [12+1024, 12+1024+1024*1024 - 1]
    uint32_t double_start = single_end + 1;
    uint32_t double_end = 12 + 1024 + (1024 * 1024) - 1;

    if (n > double_end) {
        fprintf(stderr, "Error: Block index out of range.\n");
        // roll back
        free_bitmap_bit(block_bitmap, new_data_block);
        gd->free_blocks_count++;
        return -1;
    }

    // if we reach here, 1036 <= n <= double_end
    uint32_t di_offset = n - double_start; // offset into double-indirect region
    // Each single-indirect block can hold 1024 references, so:
    uint32_t si_index   = di_offset / 1024;   // which single-indirect block inside double_indirect
    uint32_t si_offset2 = di_offset % 1024;   // index within that single-indirect block

    // If double_indirect == 0, allocate it
    if (node->double_indirect == 0) {
        int di_block = find_and_allocate_free_block(block_bitmap, gd);
        if (di_block < 0) {
            fprintf(stderr, "Error: No free blocks for double_indirect.\n");
            free_bitmap_bit(block_bitmap, new_data_block);
            gd->free_blocks_count++;
            return -1;
        }
        node->double_indirect = di_block;
        zero_block_on_disk(disk, (uint32_t)di_block);
    }

    // Now, read the block number of the 'si_index'-th single-indirect block from the double_indirect block.
    uint32_t si_block_num;
    if (read_block_reference(disk, node->double_indirect, si_index, &si_block_num) != 0) {
        fprintf(stderr, "Error: Could not read from double_indirect block.\n");
        free_bitmap_bit(block_bitmap, new_data_block);
        gd->free_blocks_count++;
        return -1;
    }

    // If si_block_num == 0, allocate a new single-indirect block
    if (si_block_num == 0) {
        int new_si_block = find_and_allocate_free_block(block_bitmap, gd);
        if (new_si_block < 0) {
            fprintf(stderr, "Error: No free blocks for double_indirect's single-indirect.\n");
            free_bitmap_bit(block_bitmap, new_data_block);
            gd->free_blocks_count++;
            return -1;
        }
        // store it in the double_indirect block
        if (write_block_reference(disk, node->double_indirect, si_index, (uint32_t)new_si_block) != 0) {
            fprintf(stderr, "Error: Could not write new_si_block reference.\n");
            free_bitmap_bit(block_bitmap, new_data_block);
            gd->free_blocks_count++;
            // also free new_si_block
            free_bitmap_bit(block_bitmap, new_si_block);
            gd->free_blocks_count++;
            return -1;
        }
        si_block_num = (uint32_t)new_si_block;
        zero_block_on_disk(disk, si_block_num);
    }

    // Finally, write the 'new_data_block' into the chosen single_indirect block at index si_offset2
    if (write_block_reference(disk, si_block_num, si_offset2, (uint32_t)new_data_block) != 0) {
        fprintf(stderr, "Error: Could not write to single_indirect block in double_indirect.\n");
        free_bitmap_bit(block_bitmap, new_data_block);
        gd->free_blocks_count++;
        return -1;
    }

    return new_data_block;
}


// [END OF HELPER FUNCTIONS]




// [MAIN FUNCTIONS]
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

// Initialize the drive with the superblock, group descriptors, and other structures:
// 1. Super Block: 1 block
// 2. Group Descriptor: 1 block
// 3. Data Block Bitmap: 1 block
// 4. Inode Bitmap: 1 block
// 5. Inode Table: Many blocks
// 6. Data Blocks: Remaining blocks
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
    group_descriptor gd;
    initialize_descriptor_block(&gd, 2, 3, 4, BLOCKS_PER_GROUP - 5, INODES_PER_GROUP, 0);
    fseek(file, BLOCK_SIZE, SEEK_SET);
    fwrite(&gd, sizeof(struct group_descriptor), 1, file);

    // Write the Data Block Bitmap
    uint8_t *data_block_bitmap = (uint8_t *) malloc(BLOCKS_COUNT / 8);
    initialize_bitmap(data_block_bitmap, BLOCKS_COUNT);
    fseek(file, 2 * BLOCK_SIZE, SEEK_SET);
    fwrite(data_block_bitmap, BLOCKS_COUNT / 8, 1, file);

    // Write the inode Bitmap
    uint8_t *inode_bitmap = (uint8_t *) malloc(INODES_COUNT / 8);
    initialize_bitmap(inode_bitmap, INODES_COUNT);
    fseek(file, 3 * BLOCK_SIZE, SEEK_SET);
    fwrite(inode_bitmap, INODES_COUNT / 8, 1, file);

    // Write the inode Table
    inode_table inode_table;
    initialize_inode_table(&inode_table);
    fseek(file, 4 * BLOCK_SIZE, SEEK_SET);
    fwrite(&inode_table, sizeof(inode_table), 1, file);

    fclose(file);
}

// Create a directory on the drive:
// 1. Opens the drive file in read/write mode.
// 2. Reads Group Descriptor, Block Bitmap, and Inode Bitmap, Inode Table.
// 3. Finds a free block and marks it allocated
// 4. Allocates a new inode for the directory, sets its first block
// 5. Initializes a 'directory' structure on that block
// 6. Updates the Group Descriptor and writes it all back to disk.
void create_directory(const char *filename, 
                      const char *dir_name, 
                      uint32_t permissions,
                      uint32_t parent_inode_number) {
    FILE *disk = fopen(filename, "rb+");
    if (!disk) {
        fprintf(stderr, "Error: cannot open file %s\n", filename);
        return;
    }

    // 1. Read the group descriptor
    group_descriptor gd;
    fseek(disk, BLOCK_SIZE, SEEK_SET);
    fread(&gd, sizeof(group_descriptor), 1, disk);

    // 2. Read the block bitmap
    uint8_t *block_bitmap = (uint8_t *)malloc(BLOCKS_COUNT / 8);
    fseek(disk, gd.block_bitmap * BLOCK_SIZE, SEEK_SET);
    fread(block_bitmap, BLOCKS_COUNT / 8, 1, disk);

    // 3. Read the inode bitmap
    uint8_t *inode_bitmap = (uint8_t *)malloc(INODES_COUNT / 8);
    fseek(disk, gd.inode_bitmap * BLOCK_SIZE, SEEK_SET);
    fread(inode_bitmap, INODES_COUNT / 8, 1, disk);

    // 4. Read the inode table
    inode_table itable;
    fseek(disk, gd.inode_table * BLOCK_SIZE, SEEK_SET);
    fread(&itable, sizeof(inode_table), 1, disk);

    // 5. Allocate a new directory inode
    inode *dir_inode = allocate_inode(&itable, inode_bitmap, 1, permissions);

    if (!dir_inode) {
        fprintf(stderr, "Error: cannot allocate inode for directory\n");
        goto cleanup;
    }
    gd.free_inodes_count--;
    gd.used_dirs_count++;

    // 6. Build in-memory directory block with "." and ".."
    directory_block_t *dirblk = create_minimal_directory_block(dir_inode->inode_number, parent_inode_number);
    if (!dirblk) {
        fprintf(stderr, "Error: could not create minimal directory block in memory.\n");
        // Roll back the inode
        deallocate_inode(&itable, inode_bitmap, dir_inode->inode_number);
        gd.free_inodes_count++;
        gd.used_dirs_count--;
        goto cleanup;
    }

    // Calculate the number of blocks needed for the directory
    size_t dirblk_size = sizeof(directory_block_t) + dirblk->entries_count * sizeof(dir_entry_t);
    size_t needed_blocks = (dirblk_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    dir_inode->file_size = 0;

    // 7. Allocate each needed block
    uint8_t *src_ptr = (uint8_t *)dirblk;
    for (size_t i = 0; i < needed_blocks; i++) {
        // Use the extended allocate_data_block_for_inode
        int allocated_block = allocate_data_block_for_inode(disk, dir_inode, i, block_bitmap, &gd);
        if (allocated_block < 0) {
            fprintf(stderr, "Error: could not allocate data block for directory.\n");
            // Roll back the inode
            deallocate_inode(&itable, inode_bitmap, dir_inode->inode_number);
            gd.free_inodes_count++;
            gd.used_dirs_count--;
            // Roll back the directory block
            free(dirblk);
            goto cleanup;
        }

        // Write the slice of the dirblk that fits in this block
        size_t offset = i * BLOCK_SIZE;
        size_t bytes_left = dirblk_size - offset;
        size_t to_write = (bytes_left > BLOCK_SIZE) ? BLOCK_SIZE : bytes_left;

        fseek(disk, (FIRST_DATA_BLOCK + allocated_block) * BLOCK_SIZE, SEEK_SET);
        fwrite(src_ptr + offset, to_write, 1, disk);

        dir_inode->file_size += to_write;
    }

    free(dirblk);

    // 8) Overwrite updated metadata structures
    // Group Descriptor
    fseek(disk, BLOCK_SIZE, SEEK_SET);
    fwrite(&gd, sizeof(gd), 1, disk);

    // Block Bitmap
    fseek(disk, gd.block_bitmap * BLOCK_SIZE, SEEK_SET);
    fwrite(block_bitmap, BLOCKS_COUNT / 8, 1, disk);

    // Inode Bitmap
    fseek(disk, gd.inode_bitmap * BLOCK_SIZE, SEEK_SET);
    fwrite(inode_bitmap, INODES_COUNT / 8, 1, disk);

    // Inode Table
    fseek(disk, gd.inode_table * BLOCK_SIZE, SEEK_SET);
    fwrite(&itable, sizeof(itable), 1, disk);

    printf("Directory '%s' created (inode #%u). Size=%u bytes.\n", dir_name, dir_inode->inode_number, dir_inode->file_size);

cleanup:
    free(block_bitmap);
    free(inode_bitmap);
    fclose(disk);
}

// Create a file by allocating an inode and required data blocks
void create_file(FILE *file, uint8_t *inode_bitmap, int *inode_table, uint8_t *block_bitmap, size_t block_count, file_t *file_info) {
    // Find a free inode
    int inode_index = -1;
    for (size_t i = 0; i < MAX_INODE_COUNT; i++) {
        if (!is_used(inode_bitmap, i)) {
            inode_index = i;
            set_used(inode_bitmap, i);
            break;
        }
    }

    if (inode_index == -1) {
        fprintf(stderr, "Error: No free inodes available.\n");
        return;
    }

    size_t data_length = file_info->size;
    size_t blocks_needed = (data_length + BLOCK_SIZE - 1) / BLOCK_SIZE; // Round up to nearest block
    int *allocated_blocks = (int *)malloc(blocks_needed * sizeof(int));
    if (allocated_blocks == NULL) {
        fprintf(stderr, "Error: Memory allocation failed.\n");
        return;
    }

    // Find free blocks
    size_t allocated_count = 0;
    for (size_t i = 0; i < block_count && allocated_count < blocks_needed; i++) {
        if (!is_used(block_bitmap, i)) {
            allocated_blocks[allocated_count++] = i;
            set_used(block_bitmap, i);
        }
    }

    if (allocated_count < blocks_needed) {
        fprintf(stderr, "Error: Not enough free blocks available.\n");
        for (size_t i = 0; i < allocated_count; i++) {
            set_free(block_bitmap, allocated_blocks[i]);
        }
        set_free(inode_bitmap, inode_index);
        free(allocated_blocks);
        return;
    }

    // Assign the first block to the inode
    inode_table[inode_index] = allocated_blocks[0];

    // Write file metadata to the allocated block
    fseek(file, allocated_blocks[0] * BLOCK_SIZE, SEEK_SET);
    fwrite(file_info, sizeof(file_t), 1, file);

    // Write file data to remaining allocated blocks
    size_t bytes_written = 0;
    for (size_t i = 1; i < blocks_needed; i++) {
        fseek(file, allocated_blocks[i] * BLOCK_SIZE, SEEK_SET);
        size_t bytes_to_write = (data_length - bytes_written > BLOCK_SIZE) ? BLOCK_SIZE : (data_length - bytes_written);
        fwrite(((char *)file_info) + sizeof(file_t) + bytes_written, 1, bytes_to_write, file);
        bytes_written += bytes_to_write;
    }

    printf("File created with inode %d and %zu blocks.\n", inode_index, blocks_needed);
    printf("Metadata written for file: %s.%s\n", file_info->name, file_info->extension);

    free(allocated_blocks);
}

// Delete a file by freeing its inode and data block
void delete_file(FILE *file, uint8_t *inode_bitmap, int *inode_table, uint8_t *block_bitmap, int inode_index) {
    if (inode_index < 0 || inode_index >= MAX_INODE_COUNT || !is_used(inode_bitmap, inode_index)) {
        fprintf(stderr, "Error: Invalid inode index or inode not in use.\n");
        return;
    }

    // Free the block associated with the inode
    int block_index = inode_table[inode_index];
    if (block_index >= 0 && block_index < BLOCKS_COUNT) {
        set_free(block_bitmap, block_index);
    }

    // Free the inode
    set_free(inode_bitmap, inode_index);
    inode_table[inode_index] = -1;

    printf("File with inode %d deleted successfully.\n", inode_index);
}

int main() {
    create_drive_file(FILENAME, BLOCK_SIZE * BLOCKS_COUNT);
    initialize_drive(FILENAME);
}