
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
# define MAX_INODE_COUNT 1024
# define FIRST_DATA_BLOCK (4 + INODES_COUNT * INODE_SIZE / BLOCK_SIZE + 1)


// [HELPER FUNCTIONS]
// Allocate a new inode in the inode table
inode *allocate_inode(inode_table *itable,
                      uint8_t *inode_bitmap,
                      group_descriptor *gd,
                      uint32_t file_type,
                      uint32_t permissions) 
{
    // 1. Quick check: if all inodes are in use at group level
    if (gd->free_inodes_count == 0) {
        fprintf(stderr, "Error: No free inodes available in the group.\n");
        return NULL;
    }

    // 2. Also check local usage in inode_table struct
    if (itable->used_inodes >= INODES_COUNT) {
        fprintf(stderr, "Error: Inode table is full.\n");
        return NULL;
    }

    // 3. Scan for a free bit in the inode bitmap
    for (uint32_t i = 0; i < INODES_COUNT; i++) {
        if (is_bit_free(inode_bitmap, i)) {
            // Mark this bit as used
            set_bitmap_bit(inode_bitmap, i);

            // Decrement group descriptor's free inodes count
            gd->free_inodes_count--;

            // If it's a directory (by convention file_type=1), increment used_dirs_count
            if (file_type == 1) {
                gd->used_dirs_count++;
            }

            // Increment the local used_inodes count
            itable->used_inodes++;

            // Initialize the inode structure
            inode *new_node = &itable->inodes[i];
            initialize_inode(new_node, i, file_type, permissions);

            return new_node;
        }
    }

    // 4. If we exit the loop, there was no free inode
    printf("Error: Inode bitmap indicates free inodes, but none found.\n");
    return NULL;
}

// Deallocate an inode in the inode table
void deallocate_inode(inode_table *itable,
                      uint8_t *inode_bitmap,
                      group_descriptor *gd,
                      uint32_t inode_number) 
{
    // 1. Validate the inode_number
    if (inode_number == 0 || inode_number > INODES_COUNT) {
        printf("Error: Invalid inode number %u. \n", inode_number);
        return;
    }

    uint32_t index = inode_number - 1;

    // 2. Check if the bitmap bit is actually set
    if (!is_bit_free(inode_bitmap, index)) {
        // For reporting, grab the inode before zeroing it
        inode *old_inode = &itable->inodes[index];
        uint32_t old_file_type = old_inode->file_type;

        // Free the bit in the bitmap
        free_bitmap_bit(inode_bitmap, index);

        // If this was a directory, decrement used_dirs_count
        if (old_file_type == 1) {
            gd->used_dirs_count--;
        }

        // Clear the inode structure
        memset(&itable->inodes[index], 0, sizeof(inode));

        // Decrement the local used_inodes count
        if (itable->used_inodes > 0) {
            itable->used_inodes--;
        }
    } else {
        printf("Error: Inode %u is not allocated.\n", inode_number);
    }
}

// Find a free block and allocate it
int find_and_allocate_free_block(uint8_t *block_bitmap, group_descriptor *gd) {
    int free_index = find_free_block(block_bitmap, BLOCKS_COUNT, 1);
    if (free_index < 0) {
        fprintf(stderr, "Error: No free blocks available.\n");
        return -1;
    }

    set_bitmap_bit(block_bitmap, free_index);
    gd->free_blocks_count--;

    return free_index;
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

// Frees(deallocates) the given block in the block bitmap.
static void free_data_block(uint8_t *block_bitmap, group_descriptor *gd, int block_idx) {
    free_bitmap_bit(block_bitmap, block_idx);
    gd->free_blocks_count++;
}

// Free all data blocks (direct, single-indirect, double-indirect) used by 'node'.
void free_all_data_blocks_of_inode(FILE *disk,
                                   inode *node,
                                   uint8_t *block_bitmap,
                                   group_descriptor *gd)
{
    // 1. Free Direct blocks
    for (int i = 0; i < 12; i++) {
        if (node->blocks[i] != 0) {
            free_data_block(block_bitmap, gd, node->blocks[i]);
            node->blocks[i] = 0;
        }
    }

    // 2. Free Single-Indirect blocks
    if (node->single_indirect != 0) {
        uint32_t block_ref;
        for (int i = 0; i < BLOCK_SIZE / sizeof(uint32_t); i++) {
            if (read_block_reference(disk, node->single_indirect, i, &block_ref) != 0) {
                fprintf(stderr, "Warning: failed to read single-indirect block #%u index %d.\n",
                        node->single_indirect, i);
                break;
            }
            if (block_ref != 0) {
                free_data_block(block_bitmap, gd, block_ref);
            }
        }
        // free the signle-indirect block itself
        free_data_block(block_bitmap, gd, node->single_indirect);
        node->single_indirect = 0;
    }

    // 3. Free Double-Indirect blocks
    if (node->double_indirect != 0) {
        // The double-indirect block is an array of up to 1024 references,
        // each pointing to a single-indirect block.
        uint32_t si_block_num;
        for (int i = 0; i < BLOCK_SIZE /sizeof(uint32_t); i++) {
            if (read_block_reference(disk, node->double_indirect, i, &si_block_num) != 0) {
                fprintf(stderr, "Warning: failed to read double-indirect block #%u index %d.\n",
                        node->double_indirect, i);
                break;
            }
            if (si_block_num != 0) {
                uint32_t block_ref;
                // For each single-indirect block, free up the blocks
                for (int j = 0; j < BLOCK_SIZE / sizeof(uint32_t); j++) {
                    if (read_block_reference(disk, si_block_num, j, &block_ref) != 0) {
                        fprintf(stderr, "Warning: failed to read single-indirect block #%u index %d.\n",
                                si_block_num, j);
                        break;
                    }
                    if (block_ref != 0) {
                        free_data_block(block_bitmap, gd, block_ref);
                    }
                }
                // free the single-indirect block itself
                free_data_block(block_bitmap, gd, si_block_num);
            }
        }
        // now free the double-indirect block itself
        free_data_block(block_bitmap, gd, node->double_indirect);
        node->double_indirect = 0;
    }
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
    // 1. Open the file for read/write
    FILE *file = fopen(filename, "rb+");
    if (file == NULL) {
        fprintf(stderr, "Error: Unable to open file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    // 2. Build all structures in memory first
    // superblock
    superblock sb;
    initialize_superblock(
        &sb,
        BLOCKS_COUNT,
        INODES_COUNT,
        BLOCK_SIZE,
        INODE_SIZE,
        BLOCKS_COUNT,
        INODES_COUNT,
        FIRST_DATA_BLOCK,
        "1234567890abcdef",
        "MyDrive",
        0xEF53
    );

    // group descriptor
    group_descriptor gd;
    initialize_descriptor_block(
        &gd,
        2, // block_bitmap
        3, // inode_bitmap
        4, // inode_table
        BLOCKS_COUNT - FIRST_DATA_BLOCK + 1,
        INODES_COUNT,
        0  // used_dirs_count
    );

    //data block bitmap (in memory)
    uint8_t *data_block_bitmap = (uint8_t *) malloc(BLOCKS_COUNT / 8 + 1);
    initialize_bitmap(data_block_bitmap, BLOCKS_COUNT);
    
    // inode bitmap (in memory)
    uint8_t *inode_bitmap = (uint8_t * ) malloc(INODES_COUNT / 8);
    initialize_bitmap(inode_bitmap, INODES_COUNT);

    // inode table
    inode_table itable;
    initialize_inode_table(&itable);

    // 3. Allocate the root directory
    // Allocate the root inode (file_type=1 for directory, permissions=0755)
    inode *root_inode = allocate_inode(&itable, inode_bitmap, &gd, 1, 0755);
    if (!root_inode) {
        fprintf(stderr, "Error: Could not allocate root directory inode. \n");
        free(data_block_bitmap);
        free(inode_bitmap);
        fclose(file);
        exit(EXIT_FAILURE);
    }

    // Build a minimal directory block (with '.' and '..')
    directory_block_t *root_dir_block = create_minimal_directory_block(
        root_inode->inode_number,   // '.' points to itself
        root_inode->inode_number    // '..' also points to ifself for root
    );
    if (!root_dir_block) {
        fprintf(stderr, "Error: Could not build root directory block.\n");
        free(data_block_bitmap);
        free(inode_bitmap);
        fclose(file);
        exit(EXIT_FAILURE);
    }
    size_t root_dir_size = sizeof(directory_block_t)
                         + root_dir_block->entries_count * sizeof(dir_entry_t);
    
    // Allocate a data block for the root directory contents
    int free_block_index = find_free_block(data_block_bitmap, BLOCKS_COUNT, 1);
    if (free_block_index < 0) {
        fprintf(stderr, "Error: No free data blocks for root directory.\n");
        free(root_dir_block);
        free(data_block_bitmap);
        free(inode_bitmap);
        fclose(file);
        exit(EXIT_FAILURE);
    }
    set_bitmap_bit(data_block_bitmap, free_block_index);
    gd.free_blocks_count--;

    // Update the root inode with this block
    root_inode->blocks[0] = free_block_index;
    root_inode->file_size = (uint32_t)root_dir_size;

    // 4. Write the blocks into the disk
    // Super block
    fseek(file, 0, SEEK_SET);
    fwrite(&sb, sizeof(superblock), 1, file);

    // Group Descriptor
    fseek(file, BLOCK_SIZE, SEEK_SET);
    fwrite(&gd, sizeof(group_descriptor), 1, file);

    // Block Bitmap
    fseek(file, gd.block_bitmap * BLOCK_SIZE, SEEK_SET);
    fwrite(data_block_bitmap, BLOCKS_COUNT / 8, 1, file);

    // Inode Bitmap
    fseek(file, gd.inode_bitmap * BLOCK_SIZE, SEEK_SET);
    fwrite(inode_bitmap, INODES_COUNT / 8, 1, file);

    // Inode Table
    fseek(file, gd.inode_table * BLOCK_SIZE, SEEK_SET);
    fwrite(&itable, sizeof(itable), 1, file);

    // Root Directory
    fseek(file, (long)(free_block_index + FIRST_DATA_BLOCK) * BLOCK_SIZE, SEEK_SET);
    fwrite(root_dir_block, root_dir_size, 1, file);

    // 5. Clean up in-memory structures
    free(root_dir_block);
    free(data_block_bitmap);
    free(inode_bitmap);

    fclose(file);
    printf("Drive initialized successfully with root directory at inode #%u (block %d).\n",
           root_inode->inode_number, (int)FIRST_DATA_BLOCK + free_block_index);
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
    inode *dir_inode = allocate_inode(&itable, inode_bitmap, &gd, 1, permissions);

    if (!dir_inode) {
        fprintf(stderr, "Error: cannot allocate inode for directory\n");
        goto cleanup;
    }

    // 6. Build in-memory directory block with "." and ".."
    directory_block_t *dirblk = create_minimal_directory_block(dir_inode->inode_number, parent_inode_number);
    if (!dirblk) {
        fprintf(stderr, "Error: could not create minimal directory block in memory.\n");
        // Roll back the inode
        deallocate_inode(&itable, inode_bitmap, &gd, dir_inode->inode_number);
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
            deallocate_inode(&itable, inode_bitmap, &gd, dir_inode->inode_number);
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

void delete_directory(const char *filename, uint32_t dir_inode_number) {
    // 1. Open the disk file
    FILE *disk = fopen(filename, "rb+");
    if (!disk) {
        fprintf(stderr, "Error: cannot open file %s\n", filename);
        return;
    }

    // 2. Read the group descriptor
    group_descriptor gd;
    fseek(disk, BLOCK_SIZE, SEEK_SET);
    fread(&gd, sizeof(group_descriptor), 1, disk);

    // 3. Read the block bitmap
    uint8_t *block_bitmap = (uint8_t *)malloc(BLOCKS_COUNT / 8);
    fseek(disk, gd.block_bitmap * BLOCK_SIZE, SEEK_SET);
    fread(block_bitmap, BLOCKS_COUNT / 8, 1, disk);

    // 4. Read the inode bitmap
    uint8_t *inode_bitmap = (uint8_t *)malloc(INODES_COUNT / 8);
    fseek(disk, gd.inode_bitmap * BLOCK_SIZE, SEEK_SET);
    fread(inode_bitmap, INODES_COUNT / 8, 1, disk);

    // 5. Read the inode table
    inode_table itable;
    fseek(disk, gd.inode_table * BLOCK_SIZE, SEEK_SET);
    fread(&itable, sizeof(inode_table), 1, disk);


    // Validate dir_inode_number
    if (dir_inode_number == 0 || dir_inode_number > INODES_COUNT) {
        fprintf(stderr, "Error: invalid inode number %u\n", dir_inode_number);
        goto cleanup;
    }

    // 6. Grab a pointer to the indoe structure
    inode *dir_inode = &itable.inodes[dir_inode_number - 1];

    // Check if this inode is actually allocated
    if (is_bit_free(inode_bitmap, dir_inode_number - 1)) {
        fprintf(stderr, "Error: inode #%u is not allocated.\n", dir_inode_number);
        goto cleanup;
    }

    // Optional: check if dir_inode->file_type == 1 (directory)
    if (dir_inode->file_type != 1) {
        fprintf(stderr, "Error: Inode #%u is not a directory (file_type=%u).\n",
                dir_inode_number, dir_inode->file_type);
        goto cleanup;
    }

    // 7. Free all blocks used by this directory (directory, single-indirect, double-indirect)
    free_all_data_blocks_of_inode(disk, dir_inode, block_bitmap, &gd);

    // 8. Deallocate the inode
    deallocate_inode(&itable, inode_bitmap, &gd, dir_inode_number);

    // 9. Write everything back
    // group descriptor
    fseek(disk, BLOCK_SIZE, SEEK_SET);
    fwrite(&gd, sizeof(gd), 1, disk);

    // block bitmap
    fseek(disk, gd.block_bitmap * BLOCK_SIZE, SEEK_SET);
    fwrite(block_bitmap, BLOCKS_COUNT / 8, 1, disk);

    // inode bitmap
    fseek(disk, gd.inode_bitmap * BLOCK_SIZE, SEEK_SET);
    fwrite(inode_bitmap, INODES_COUNT / 8, 1, disk);

    // inode table
    fseek(disk, gd.inode_table * BLOCK_SIZE, SEEK_SET);
    fwrite(&itable, sizeof(itable), 1, disk);

    printf("Directory inode #%u deleted successfully.\n", dir_inode_number);

cleanup:
    free(block_bitmap);
    free(inode_bitmap);
    fclose(disk);
}

void create_file(const char *filename, 
                 const char *file_name, 
                 const char *extension, 
                 uint32_t permissions, 
                 uint32_t parent_inode_number, 
                 const char *data, 
                 uint64_t file_size) {
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

    // 5. Create the file_t structure
    size_t data_size = sizeof(file_t) + strlen(data);
    file_t *file_data = (file_t *)malloc(data_size);
    if (!file_data) {
        fprintf(stderr, "Error: could not allocate memory for file metadata\n");
        goto cleanup;
    }

    // Initialize the file_t structure
    strncpy(file_data->name, file_name, sizeof(file_data->name) - 1);
    strncpy(file_data->extension, extension, sizeof(file_data->extension) - 1);
    file_data->type = 0; // Regular file
    file_data->size = strlen(data);

    // Calculate the number of blocks needed for the entire file metadata + data
    size_t needed_blocks = (data_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // Allocate a new file inode
    inode *file_inode = allocate_inode(&itable, inode_bitmap, &gd, 0, permissions);
    if (!file_inode) {
        fprintf(stderr, "Error: cannot allocate inode for file\n");
        free(file_data);
        goto cleanup;
    }
    file_data->inode = file_inode->inode_number;

    // Write the file metadata and data to the disk
    memcpy(file_data->data, data, strlen(data));

    // Set the inode file size
    file_inode->file_size = (uint32_t)data_size;

    // Add file to the parent directory's directory block
    // Step 1: Locate the parent directory inode
    inode *parent_inode = &itable.inodes[parent_inode_number - 1];
    if (!parent_inode || parent_inode->file_type != 1) {
        fprintf(stderr, "Error: invalid parent directory inode\n");
        free(file_data);
        goto cleanup;
    }

    // Step 2: Load the directory block from the parent directory
    uint32_t dir_block_index = parent_inode->blocks[0]; // Assuming only one block for simplicity
    if (dir_block_index == 0) {
        fprintf(stderr, "Error: parent directory has no data blocks\n");
        free(file_data);
        goto cleanup;
    }

    directory_block_t *dir_block = (directory_block_t *)malloc(BLOCK_SIZE);
    if (!dir_block) {
        fprintf(stderr, "Error: could not allocate memory for directory block\n");
        free(file_data);
        goto cleanup;
    }

    fseek(disk, (FIRST_DATA_BLOCK + dir_block_index) * BLOCK_SIZE, SEEK_SET);
    fread(dir_block, BLOCK_SIZE, 1, disk);

    // Step 3: Add new directory entry
    if (dir_block->entries_count >= BLOCK_SIZE / sizeof(dir_entry_t)) {
        fprintf(stderr, "Error: directory block is full\n");
        free(file_data);
        free(dir_block);
        goto cleanup;
    }

    dir_entry_t *new_entry = &dir_block->entries[dir_block->entries_count++];
    new_entry->inode = file_inode->inode_number;
    new_entry->rec_len = sizeof(dir_entry_t);
    new_entry->name_len = strlen(file_name);
    new_entry->file_type = 0; // Regular file
    strncpy(new_entry->name, file_name, MAX_FILENAME_LEN);

    // Step 4: Write the updated directory block back to disk
    fseek(disk, (FIRST_DATA_BLOCK + dir_block_index) * BLOCK_SIZE, SEEK_SET);
    fwrite(dir_block, BLOCK_SIZE, 1, disk);

    free(dir_block);
    // 6. Allocate each needed block and write the file metadata/data
    uint8_t *src_ptr = (uint8_t *)file_data;
    size_t bytes_written = 0;
    for (size_t i = 0; i < needed_blocks; i++) {
        // Use the extended allocate_data_block_for_inode
        int allocated_block = allocate_data_block_for_inode(disk, file_inode, i, block_bitmap, &gd);
        if (allocated_block < 0) {
            fprintf(stderr, "Error: could not allocate data block for file.\n");
            // Roll back the inode
            deallocate_inode(&itable, inode_bitmap, &gd, file_inode->inode_number);
            free(file_data);
            goto cleanup;
        }

        // Write the slice of the file_metadata that fits in this block
        size_t offset = i * BLOCK_SIZE;
        size_t bytes_left = data_size - offset;
        size_t to_write = (bytes_left > BLOCK_SIZE) ? BLOCK_SIZE : bytes_left;

        fseek(disk, (FIRST_DATA_BLOCK + allocated_block) * BLOCK_SIZE, SEEK_SET);
        fwrite(src_ptr + offset, to_write, 1, disk);

        bytes_written += to_write;
    }

    free(file_data);

    // 7. Overwrite updated metadata structures
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

    printf("File '%s' created (inode #%u). Size=%llu bytes.\n", file_name, file_inode->inode_number, file_size);

cleanup:
    free(block_bitmap);
    free(inode_bitmap);
    fclose(disk);
}

void delete_file(const char *filename, uint32_t inode_number) {
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

    // 5. Validate inode number
    if (inode_number == 0 || inode_number > INODES_COUNT) {
        fprintf(stderr, "Error: invalid inode number %u\n", inode_number);
        goto cleanup;
    }

    inode *file_inode = &itable.inodes[inode_number - 1];

    // Check if the inode is allocated
    if (is_bit_free(inode_bitmap, inode_number - 1)) {
        fprintf(stderr, "Error: inode #%u is not allocated.\n", inode_number);
        goto cleanup;
    }

    // Free all data blocks used by the file
    free_all_data_blocks_of_inode(disk, file_inode, block_bitmap, &gd);

    // Deallocate the inode
    deallocate_inode(&itable, inode_bitmap, &gd, inode_number);

    // Write updated metadata back to disk
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

    printf("File with inode #%u deleted successfully.\n", inode_number);

cleanup:
    free(block_bitmap);
    free(inode_bitmap);
    fclose(disk);
}

int main() {
    create_drive_file(FILENAME, BLOCK_SIZE * BLOCKS_COUNT);
    initialize_drive(FILENAME);
}