
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "superblock.h"
#include "group_descriptor.h"
#include "bitmap.h"
#include "inode.h"
#include "file.h"

# define DRIVE_NAME "drive.bin"
# define BLOCK_SIZE 4096
# define BLOCKS_COUNT 32768
# define MAX_INODE_COUNT 1024
# define FIRST_DATA_BLOCK (4 + INODES_COUNT * INODE_SIZE / BLOCK_SIZE + 1)
#define MAX_INPUT_SIZE 1024

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

/**
 * Reads data from an inode into a buffer.
 *
 * This function reads the data blocks associated with an inode from a disk
 * image file and stores the data in the provided buffer. It handles direct,
 * single-indirect, and double-indirect blocks.
 *
 * @param disk A pointer to the FILE object representing the disk image.
 * @param node A pointer to the inode structure containing block information.
 * @param buffer A pointer to the buffer where the read data will be stored.
 * @param size The maximum number of bytes to read into the buffer.
 * @return The number of bytes read into the buffer.
 */
int read_inode_data(FILE *disk, inode *node, char *buffer, size_t size) {
    size_t bytes_read = 0;

    // 1. Read direct blocks
    for (int i = 0; i < 12; i++) {
        if (node->blocks[i] == 0) break;

        size_t to_read = (size - bytes_read) > BLOCK_SIZE ? BLOCK_SIZE : (size - bytes_read);

        fseek(disk, (FIRST_DATA_BLOCK + node->blocks[i]) * BLOCK_SIZE, SEEK_SET);
        fread(buffer + bytes_read, to_read, 1, disk);

        bytes_read += to_read;
        if (bytes_read >= size) break;
    }

    // 2. Read single-indirect blocks
    if (node->single_indirect != 0 && bytes_read < size) {
        uint32_t single_indirect_blocks[BLOCK_SIZE / sizeof(uint32_t)];
        fseek(disk, (FIRST_DATA_BLOCK + node->single_indirect) * BLOCK_SIZE, SEEK_SET);
        fread(single_indirect_blocks, BLOCK_SIZE, 1, disk);

        for (int i = 0; i < BLOCK_SIZE / sizeof(uint32_t); i++) {
            if (single_indirect_blocks[i] == 0) break;

            size_t to_read = (size - bytes_read) > BLOCK_SIZE ? BLOCK_SIZE : (size - bytes_read);

            fseek(disk, (FIRST_DATA_BLOCK + single_indirect_blocks[i]) * BLOCK_SIZE, SEEK_SET);
            fread(buffer + bytes_read, to_read, 1, disk);

            bytes_read += to_read;
            if (bytes_read >= size) break;
        }
    }

    // 6. Read double-indirect blocks
    if (node->double_indirect != 0 && bytes_read < size) {
        uint32_t double_indirect_blocks[BLOCK_SIZE / sizeof(uint32_t)];
        fseek(disk, (FIRST_DATA_BLOCK + node->double_indirect) * BLOCK_SIZE, SEEK_SET);
        fread(double_indirect_blocks, BLOCK_SIZE, 1, disk);

        for (int i = 0; i < BLOCK_SIZE / sizeof(uint32_t); i++) {
            if (double_indirect_blocks[i] == 0) break;

            uint32_t single_indirect_blocks[BLOCK_SIZE / sizeof(uint32_t)];
            fseek(disk, (FIRST_DATA_BLOCK + double_indirect_blocks[i]) * BLOCK_SIZE, SEEK_SET);
            fread(single_indirect_blocks, BLOCK_SIZE, 1, disk);

            for (int j = 0; j < BLOCK_SIZE / sizeof(uint32_t); j++) {
                if (single_indirect_blocks[j] == 0) break;

                size_t to_read = (size - bytes_read) > BLOCK_SIZE ? BLOCK_SIZE : (size - bytes_read);

                fseek(disk, (FIRST_DATA_BLOCK + single_indirect_blocks[j]) * BLOCK_SIZE, SEEK_SET);
                fread(buffer + bytes_read, to_read, 1, disk);

                bytes_read += to_read;
                if (bytes_read >= size) break;
            }

            if (bytes_read >= size) break;
        }
    }
}

// [END OF HELPER FUNCTIONS]




// [MAIN FUNCTIONS]

/**
 * @brief Creates a new file with the specified size.
 *
 * This function creates a new file with the given filename and sets its size to the specified value.
 * It opens the file in write-binary mode, moves the file pointer to the last byte, writes a null byte
 * to allocate the space, and then moves the file pointer back to the beginning.
 *
 * @param filename The name of the file to be created.
 * @param size The size of the file to be created in bytes.
 * @return A pointer to the created file.
 * @note If the file cannot be created, the function prints an error message and exits the program.
 */
void create_drive_file(const char *filename, uint64_t size) {
    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        fprintf(stderr, "Error: Unable to create file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    fseek(file, size - 1, SEEK_SET); // Move the file pointer to the last byte
    fputc('\0', file); // Write a null byte

    fseek(file, 0, SEEK_SET); // Move the file pointer back to the beginning

    fclose(file);
}


/**
 * @brief Initializes a drive by setting up the necessary filesystem structures.
 *
 * This function initializes a drive by creating and writing the superblock, group descriptor,
 * block bitmap, inode bitmap, inode table, and root directory to the disk.
 *
 * @param disk A pointer to the FILE object representing the disk to be initialized.
 *
 * The function performs the following steps:
 * 1. Builds all necessary structures in memory:
 *    a. Initializes the superblock with the given parameters.
 *    b. Initializes the group descriptor with the given parameters.
 *    c. Allocates and initializes the data block bitmap.
 *    d. Allocates and initializes the inode bitmap.
 *    e. Initializes the inode table.
 * 2. Allocates the root directory:
 *    a. Allocates the root inode with file type set to directory and permissions set to 0755.
 *    b. Builds a minimal directory block containing '.' and '..' entries.
 *    c. Allocates a data block for the root directory contents.
 *    d. Updates the root inode with the allocated data block and its size.
 * 3. Writes the initialized structures to the disk:
 *    a. Writes the superblock to the disk.
 *    b. Writes the group descriptor to the disk.
 *    c. Writes the block bitmap to the disk.
 *    d. Writes the inode bitmap to the disk.
 *    e. Writes the inode table to the disk.
 *    f. Writes the root directory block to the disk.
 * 4. Cleans up the in-memory structures.
 *
 * If any error occurs during the initialization process, the function prints an error message,
 * frees allocated memory, closes the disk file, and exits the program with a failure status.
 */
void initialize_drive(FILE *disk) {

    // 1. Build all structures in memory first
    // 1a. Superblock
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

    // 1b. Group Descriptor
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

    // 1c. Data block bitmap
    uint8_t *data_block_bitmap = (uint8_t *) malloc(BLOCKS_COUNT / 8 + 1);
    initialize_bitmap(data_block_bitmap, BLOCKS_COUNT);
    
    // 1d. Inode bitmap
    uint8_t *inode_bitmap = (uint8_t * ) malloc(INODES_COUNT / 8);
    initialize_bitmap(inode_bitmap, INODES_COUNT);

    // 1e. Inode Table
    inode_table itable;
    initialize_inode_table(&itable);

    // 2. Allocate the root directory
    // 2a. Allocate the root inode (file_type=1 for directory, permissions=0755)
    inode *root_inode = allocate_inode(&itable, inode_bitmap, &gd, 1, 0755);
    if (!root_inode) {
        fprintf(stderr, "Error: Could not allocate root directory inode. \n");
        free(data_block_bitmap);
        free(inode_bitmap);
        exit(EXIT_FAILURE);
    }

    // 2b. Build a minimal directory block (with '.' and '..')
    directory_block_t *root_dir_block = create_minimal_directory_block(
        root_inode->inode_number,   // '.' points to itself
        root_inode->inode_number    // '..' also points to ifself for root
    );
    if (!root_dir_block) {
        fprintf(stderr, "Error: Could not build root directory block.\n");
        free(data_block_bitmap);
        free(inode_bitmap);
        exit(EXIT_FAILURE);
    }
    size_t root_dir_size = sizeof(directory_block_t)
                         + root_dir_block->entries_count * sizeof(dir_entry_t);
    
    // 2c. Allocate a data block for the root directory contents
    int free_block_index = find_free_block(data_block_bitmap, BLOCKS_COUNT, 1);
    if (free_block_index < 0) {
        fprintf(stderr, "Error: No free data blocks for root directory.\n");
        free(root_dir_block);
        free(data_block_bitmap);
        free(inode_bitmap);
        exit(EXIT_FAILURE);
    }
    set_bitmap_bit(data_block_bitmap, free_block_index);
    gd.free_blocks_count--;

    // 2d. Update the root inode with this block
    root_inode->blocks[0] = free_block_index;
    root_inode->file_size = (uint32_t)root_dir_size;

    // 3. Write the blocks into the disk
    // 3a. Super block
    fseek(disk, 0, SEEK_SET);
    fwrite(&sb, sizeof(superblock), 1, disk);

    // 3b. Group Descriptor
    fseek(disk, BLOCK_SIZE, SEEK_SET);
    fwrite(&gd, sizeof(group_descriptor), 1, disk);

    // 3c. Block Bitmap
    fseek(disk, gd.block_bitmap * BLOCK_SIZE, SEEK_SET);
    fwrite(data_block_bitmap, BLOCKS_COUNT / 8, 1, disk);

    // 3d. Inode Bitmap
    fseek(disk, gd.inode_bitmap * BLOCK_SIZE, SEEK_SET);
    fwrite(inode_bitmap, INODES_COUNT / 8, 1, disk);

    // 3e. Inode Table
    fseek(disk, gd.inode_table * BLOCK_SIZE, SEEK_SET);
    fwrite(&itable, sizeof(itable), 1, disk);

    // 3f. Root Directory
    fseek(disk, (long)(free_block_index + FIRST_DATA_BLOCK) * BLOCK_SIZE, SEEK_SET);
    fwrite(root_dir_block, root_dir_size, 1, disk);

    // 4. Clean up in-memory structures
    free(root_dir_block);
    free(data_block_bitmap);
    free(inode_bitmap);
    fseek(disk, 0, SEEK_SET);

    printf("Drive initialized successfully with root directory at inode #%u (block %d).\n",
           root_inode->inode_number, (int)FIRST_DATA_BLOCK + free_block_index);
}


/**
 * @brief Reads a file from the disk using its inode number.
 *
 * This function reads the file data from the disk by locating the inode
 * using the provided inode number. It first reads the group descriptor
 * and inode table, validates the inode number, checks if the inode is
 * allocated and is a file, and then reads the file data into a newly
 * allocated memory buffer.
 *
 * @param disk Pointer to the disk file.
 * @param inode_number The inode number of the file to be read.
 * @return Pointer to the file data structure (file_t) on success, or NULL on failure.
 */
file_t* read_file(FILE* disk, uint32_t inode_number) {

    // 1. Read the group descriptor
    group_descriptor gd;
    fseek(disk, BLOCK_SIZE, SEEK_SET);
    fread(&gd, sizeof(group_descriptor), 1, disk);

    // 2. Read the inode table
    inode_table itable;
    fseek(disk, gd.inode_table * BLOCK_SIZE, SEEK_SET);
    fread(&itable, sizeof(inode_table), 1, disk);

    // 3. Validate inode number
    if (inode_number == 0 || inode_number > INODES_COUNT) {
        fprintf(stderr, "Error: invalid inode number %u\n", inode_number);
        return NULL;
    }

    inode *file_inode = &itable.inodes[inode_number];

    // Check if the inode is allocated
    if (file_inode->file_size == 0) {
        fprintf(stderr, "Error: inode #%u is not allocated or is empty.\n", inode_number);
        return NULL;
    }

    // Check if the inode is a file
    if (file_inode->file_type != 0) {
        fprintf(stderr, "Error: inode #%u is not a file.\n", inode_number);
        return NULL;
    }

    // 4. Allocate memory to reconstruct the file_t structure and return
    size_t file_size = file_inode->file_size;
    file_t *file_data = (file_t *)malloc(file_size);
    if (!file_data) {
        fprintf(stderr, "Error: could not allocate memory to read file.\n");
        return NULL;
    }

    if (read_inode_data(disk, file_inode, (char*) file_data, file_size) != 0) {
        fprintf(stderr, "Error: could not read file data.\n");
        free(file_data);
        return NULL;
    }

    return file_data;
}


/**
 * Reads the directory block associated with a given inode number from the disk.
 *
 * @param disk A pointer to the FILE object representing the disk.
 * @param inode_number The inode number of the directory to read.
 * @return A pointer to the directory_block_t structure containing the directory data,
 *         or NULL if an error occurs (e.g., invalid inode number, inode not allocated,
 *         inode is not a directory, memory allocation failure, or read error).
 *
 * The function performs the following steps:
 * 1. Reads the group descriptor from the disk.
 * 2. Reads the inode table from the disk.
 * 3. Validates the inode number.
 * 4. Checks if the inode is allocated and is a directory.
 * 5. Allocates memory for the directory data and reads it from the disk.
 */
directory_block_t* read_directory(FILE* disk, uint32_t inode_number) {
    // 1. Read the group descriptor
    group_descriptor gd;
    fseek(disk, BLOCK_SIZE, SEEK_SET);
    fread(&gd, sizeof(group_descriptor), 1, disk);

    // 2. Read the inode table
    inode_table itable;
    fseek(disk, gd.inode_table * BLOCK_SIZE, SEEK_SET);
    fread(&itable, sizeof(inode_table), 1, disk);

    // 3. Validate inode number
    if (inode_number > INODES_COUNT) {
        fprintf(stderr, "Error: invalid inode number %u\n", inode_number);
        return NULL;
    }

    inode *dir_inode = &itable.inodes[inode_number];

    // Check if the inode is allocated
    if (dir_inode->file_size == 0) {
        fprintf(stderr, "Error: inode #%u is not allocated or is empty.\n", inode_number);
        return NULL;
    }

    // Check if the inode is a directory
    if (dir_inode->file_type != 1) {
        fprintf(stderr, "Error: inode #%u is not a directory.\n", inode_number);
        return NULL;
    }

    // 4. Allocate memory to reconstruct the directory_block_t structure and return
    size_t dir_size = dir_inode->file_size;
    directory_block_t *dir_data = (directory_block_t *)malloc(dir_size);
    if (!dir_data) {
        fprintf(stderr, "Error: could not allocate memory to read directory.\n");
        return NULL;
    }

    if (read_inode_data(disk, dir_inode, (char*) dir_data, dir_size) != 0) {
        fprintf(stderr, "Error: could not read directory data.\n");
        free(dir_data);
        return NULL;
    }

    return dir_data;
}


/**
 * @brief Updates a directory's data blocks on disk.
 *
 * The `update_directory` function is responsible for updating the on-disk representation
 * of a directory by freeing its existing data blocks and allocating new blocks to store
 * the updated directory data. This is typically used after modifications to the directory
 * contents, such as adding or removing entries.
 *
 * @param disk           Pointer to the FILE object representing the disk image.
 * @param inode          Pointer to the inode structure corresponding to the directory.
 * @param block_bitmap   Pointer to the block bitmap array, which tracks allocated/free blocks.
 * @param gd             Pointer to the group descriptor structure containing filesystem metadata.
 * @param inode_number   The inode number of the directory being updated.
 * @param dir_block      Pointer to the `directory_block_t` structure containing the updated directory entries.
 */
void update_directory(FILE* disk,
                      inode *inode,
                      uint8_t *block_bitmap,
                      group_descriptor *gd,
                      uint32_t inode_number,
                      directory_block_t *dir_block) {
    free_all_data_blocks_of_inode(disk, inode, block_bitmap, gd);
    uint32_t needed_blocks = (sizeof(directory_block_t) + dir_block->entries_count * sizeof(dir_entry_t) + BLOCK_SIZE - 1) / BLOCK_SIZE;
    for (size_t i = 0; i < needed_blocks; i++) {
        int allocated_block = allocate_data_block_for_inode(disk, inode, i, block_bitmap, gd);
        if (allocated_block < 0) {
            fprintf(stderr, "Error: could not allocate data block for parent directory.\n");
            free(dir_block);
            return;
        }

        size_t offset = i * BLOCK_SIZE;
        size_t bytes_left = sizeof(directory_block_t) + dir_block->entries_count * sizeof(dir_entry_t) - offset;
        size_t to_write = (bytes_left > BLOCK_SIZE) ? BLOCK_SIZE : bytes_left;

        fseek(disk, (FIRST_DATA_BLOCK + allocated_block) * BLOCK_SIZE, SEEK_SET);
        fwrite((uint8_t *)dir_block + offset, to_write, 1, disk);
    }
}


/**
 * @brief Recursively deletes a directory and its contents from the file system.
 *
 * This function deletes a directory and all its contents, including subdirectories
 * and files, from the file system. It deallocates the inodes and data blocks used
 * by the directory and its contents.
 *
 * @param disk Pointer to the file representing the disk.
 * @param dir_inode_number The inode number of the directory to be deleted.
 * @param gd Pointer to the group descriptor structure.
 * @param itable Pointer to the inode table structure.
 * @param inode_bitmap Pointer to the inode bitmap.
 * @param block_bitmap Pointer to the block bitmap.
 */
void delete_directory_recur(FILE *disk, 
                            uint32_t dir_inode_number,
                            group_descriptor *gd,
                            inode_table *itable,
                            uint8_t *inode_bitmap,
                            uint8_t *block_bitmap)
{
    directory_block_t *dir_block = read_directory(disk, dir_inode_number);
    if (!dir_block) {
        fprintf(stderr, "Error: could not read directory block.\n");
        return;
    }

    for (size_t i = 0; i < dir_block->entries_count; i++) {
        dir_entry_t *entry = &dir_block->entries[i];
        if (entry->inode == 0) continue;

        // If the entry is a directory, recursively delete it
        if (entry->file_type == 1) {
            delete_directory_recur(disk, entry->inode, gd, itable, inode_bitmap, block_bitmap);
        }
        // If the entry is a file, deallocate its inode and data blocks
        else if (entry->file_type == 0) {
            inode *file_inode = &itable->inodes[entry->inode];
            free_all_data_blocks_of_inode(disk, file_inode, block_bitmap, gd);
            deallocate_inode(itable, inode_bitmap, gd, entry->inode);
        }
    }

    // Free all blocks used by this directory (directory, single-indirect, double-indirect)
    free_all_data_blocks_of_inode(disk, &itable->inodes[dir_inode_number], block_bitmap, gd);

    // Deallocate the inode
    deallocate_inode(itable, inode_bitmap, gd, dir_inode_number);

    printf("Directory inode #%u deleted successfully.\n", dir_inode_number);

    free(dir_block);
}


/**
 * delete_directory - Deletes a directory and its contents from the filesystem.
 * 
 * @disk: The file pointer to the disk image.
 * @dir_inode_number: The inode number of the directory to be deleted.
 * @parent_inode_number: The inode number of the parent directory.
 * 
 * This function performs the following steps:
 * 1. Reads necessary structures from the disk, including the group descriptor,
 *    block bitmap, inode bitmap, and inode table.
 * 2. Validates the directory inode number to ensure it is within a valid range
 *    and is allocated.
 * 3. Recursively deletes the directory and its contents.
 * 4. Updates the parent directory block to remove the entry for the deleted directory.
 * 5. Writes the updated structures back to the disk, including the group descriptor,
 *    block bitmap, inode bitmap, and inode table.
 * 
 * If any errors occur during the process, appropriate error messages are printed
 * to stderr, and the function performs cleanup before returning.
 * 
 * Note: This function assumes that the directory inode number and parent inode number
 * are valid and that the disk image is properly formatted.
 */
void delete_directory(FILE *disk, uint32_t dir_inode_number, uint32_t parent_inode_number) {

    // 1. Read necessary structures from disk
    // 1a. Read the group descriptor
    group_descriptor gd;
    fseek(disk, BLOCK_SIZE, SEEK_SET);
    fread(&gd, sizeof(group_descriptor), 1, disk);

    // 1b. Read the block bitmap
    uint8_t *block_bitmap = (uint8_t *)malloc(BLOCKS_COUNT / 8);
    fseek(disk, gd.block_bitmap * BLOCK_SIZE, SEEK_SET);
    fread(block_bitmap, BLOCKS_COUNT / 8, 1, disk);

    // 1c. Read the inode bitmap
    uint8_t *inode_bitmap = (uint8_t *)malloc(INODES_COUNT / 8);
    fseek(disk, gd.inode_bitmap * BLOCK_SIZE, SEEK_SET);
    fread(inode_bitmap, INODES_COUNT / 8, 1, disk);

    // 1d. Read the inode table
    inode_table itable;
    fseek(disk, gd.inode_table * BLOCK_SIZE, SEEK_SET);
    fread(&itable, sizeof(inode_table), 1, disk);


    // 2. Validate dir_inode_number
    if (dir_inode_number == 0 || dir_inode_number > INODES_COUNT) {
        fprintf(stderr, "Error: invalid inode number %u\n", dir_inode_number);
        goto cleanup;
    }

    inode *dir_inode = &itable.inodes[dir_inode_number];

    // Check if this inode is actually allocated
    if (is_bit_free(inode_bitmap, dir_inode_number)) {
        fprintf(stderr, "Error: inode #%u is not allocated.\n", dir_inode_number);
        goto cleanup;
    }

    // Check if this inode is a directory
    if (dir_inode->file_type != 1) {
        fprintf(stderr, "Error: Inode #%u is not a directory (file_type=%u).\n",
                dir_inode_number, dir_inode->file_type);
        goto cleanup;
    }

    // 3. Recursively delete the directory and its contents
    delete_directory_recur(disk, dir_inode_number, &gd, &itable, inode_bitmap, block_bitmap);

    // 4. Update the parent directory block to remove the entry
    directory_block_t *parent_dir_block = read_directory(disk, parent_inode_number);
    if (!parent_dir_block) {
        fprintf(stderr, "Error: could not read parent directory block.\n");
        goto cleanup;
    }

    // Write the updated parent directory block back to disk
    directory_block_t *new_parent_dir_block = remove_entry_from_directory_block(parent_dir_block, dir_inode_number);
    update_directory(disk, &itable.inodes[parent_inode_number], block_bitmap, &gd, parent_inode_number, new_parent_dir_block);

    free(parent_dir_block);
    free(new_parent_dir_block);

    // 5. Write everything back
    // 5a. Group Descriptor
    fseek(disk, BLOCK_SIZE, SEEK_SET);
    fwrite(&gd, sizeof(gd), 1, disk);

    // 5b. Block Bitmap
    fseek(disk, gd.block_bitmap * BLOCK_SIZE, SEEK_SET);
    fwrite(block_bitmap, BLOCKS_COUNT / 8, 1, disk);

    // 5c. Inode Bitmap
    fseek(disk, gd.inode_bitmap * BLOCK_SIZE, SEEK_SET);
    fwrite(inode_bitmap, INODES_COUNT / 8, 1, disk);

    // 5d. Inode Table
    fseek(disk, gd.inode_table * BLOCK_SIZE, SEEK_SET);
    fwrite(&itable, sizeof(itable), 1, disk);

    printf("Directory inode #%u deleted successfully.\n", dir_inode_number);

cleanup:
    free(block_bitmap);
    free(inode_bitmap);
    fseek(disk, 0, SEEK_SET);
}


/**
 * @brief Creates a new directory in the file system.
 *
 * This function creates a new directory with the specified name, permissions, 
 * and parent inode number in the file system represented by the given disk file.
 *
 * @param disk The file pointer to the disk image.
 * @param dir_name The name of the new directory to be created.
 * @param permissions The permissions for the new directory.
 * @param parent_inode_number The inode number of the parent directory.
 *
 * The function performs the following steps:
 * 1. Reads necessary structures from the disk, including the group descriptor, 
 *    block bitmap, inode bitmap, and inode table.
 * 2. Allocates necessary structures for the new directory in memory, including 
 *    an inode and a minimal directory block.
 * 3. Allocates the required number of blocks for the directory and writes the 
 *    directory block to the disk.
 * 4. Updates the parent directory block to include the new directory entry.
 * 5. Overwrites the updated metadata structures, including the group descriptor, 
 *    block bitmap, inode bitmap, and inode table.
 *
 * If any error occurs during the process, the function rolls back the changes 
 * and frees allocated memory.
 *
 * @note The function assumes that the disk image is properly formatted and 
 *       that the necessary structures are correctly initialized.
 */
void create_directory(FILE *disk, 
                      const char *dir_name, 
                      uint32_t permissions,
                      uint32_t parent_inode_number) {

    // 1. Read necessary structures from disk
    // 1a. Read the group descriptor
    group_descriptor gd;
    fseek(disk, BLOCK_SIZE, SEEK_SET);
    fread(&gd, sizeof(group_descriptor), 1, disk);

    // 1b. Read the block bitmap
    uint8_t *block_bitmap = (uint8_t *)malloc(BLOCKS_COUNT / 8);
    fseek(disk, gd.block_bitmap * BLOCK_SIZE, SEEK_SET);
    fread(block_bitmap, BLOCKS_COUNT / 8, 1, disk);

    // 1c. Read the inode bitmap
    uint8_t *inode_bitmap = (uint8_t *)malloc(INODES_COUNT / 8);
    fseek(disk, gd.inode_bitmap * BLOCK_SIZE, SEEK_SET);
    fread(inode_bitmap, INODES_COUNT / 8, 1, disk);

    // 1d. Read the inode table
    inode_table itable;
    fseek(disk, gd.inode_table * BLOCK_SIZE, SEEK_SET);
    fread(&itable, sizeof(inode_table), 1, disk);
    

    // 2. Allocate necessary structures for the new directory in memory
    // 2a. Inode for the new directory
    inode *dir_inode = allocate_inode(&itable, inode_bitmap, &gd, 1, permissions);
    if (!dir_inode) {
        fprintf(stderr, "Error: cannot allocate inode for directory\n");
        goto cleanup;
    }
    dir_inode->file_type = 1; // Set the file type to directory
    dir_inode->permissions = permissions;

    // 2b. Create a minimal directory block in memory
    directory_block_t *dirblk = create_minimal_directory_block(dir_inode->inode_number, parent_inode_number);
    if (!dirblk) {
        fprintf(stderr, "Error: could not create minimal directory block in memory.\n");
        // Roll back the inode
        deallocate_inode(&itable, inode_bitmap, &gd, dir_inode->inode_number);
        goto cleanup;
    }

    // 2c. Calculate the number of blocks needed for the directory
    size_t dirblk_size = sizeof(directory_block_t) + dirblk->entries_count * sizeof(dir_entry_t);
    size_t needed_blocks = (dirblk_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    dir_inode->file_size = (uint32_t)dirblk_size;

    // 3. Allocate each needed block
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

    // 4. Rewrite the parent directory block to include the new entry
    directory_block_t *parent_dir_block = read_directory(disk, parent_inode_number);
    if (!parent_dir_block) {
        fprintf(stderr, "Error: could not read parent directory block.\n");
        // Roll back the inode
        deallocate_inode(&itable, inode_bitmap, &gd, dir_inode->inode_number);
        goto cleanup;
    }

    // Add the new directory entry to the parent directory block
    if (parent_dir_block->entries_count >= BLOCK_SIZE / sizeof(dir_entry_t)) {
        fprintf(stderr, "Error: parent directory block is full.\n");
        // Roll back the inode
        deallocate_inode(&itable, inode_bitmap, &gd, dir_inode->inode_number);
        free(parent_dir_block);
        goto cleanup;
    }

    directory_block_t *new_parent_dir_block = add_entry_to_directory_block(parent_dir_block, dir_inode->inode_number, dir_name, 1);
    
    // Write the updated parent directory block back to disk
    update_directory(disk, &itable.inodes[parent_inode_number], block_bitmap, &gd, parent_inode_number, parent_dir_block);

    free(parent_dir_block);
    free(new_parent_dir_block);

    // 5. Overwrite updated metadata structures
    // 5a. Group Descriptor
    fseek(disk, BLOCK_SIZE, SEEK_SET);
    fwrite(&gd, sizeof(gd), 1, disk);

    // 5b. Block Bitmap
    fseek(disk, gd.block_bitmap * BLOCK_SIZE, SEEK_SET);
    fwrite(block_bitmap, BLOCKS_COUNT / 8, 1, disk);

    // 5c. Inode Bitmap
    fseek(disk, gd.inode_bitmap * BLOCK_SIZE, SEEK_SET);
    fwrite(inode_bitmap, INODES_COUNT / 8, 1, disk);

    // 5d. Inode Table
    fseek(disk, gd.inode_table * BLOCK_SIZE, SEEK_SET);
    fwrite(&itable, sizeof(itable), 1, disk);

    printf("Directory '%s' created (inode #%u). Size=%u bytes.\n", dir_name, dir_inode->inode_number, dir_inode->file_size);

cleanup:
    free(block_bitmap);
    free(inode_bitmap);
    fseek(disk, 0, SEEK_SET);
}


/**
 * @brief Creates a file in the specified parent directory inode.
 *
 * This function creates a file with the given name, extension, permissions, and data
 * in the specified parent directory inode. It performs the following steps:
 * 1. Reads necessary structures from the disk (group descriptor, block bitmap, inode bitmap, inode table).
 * 2. Creates the file metadata structure and allocates an inode for the file.
 * 3. Adds the file entry to the parent directory's directory block.
 * 4. Allocates the necessary blocks for the file and writes the file metadata and data to the disk.
 * 5. Updates the metadata structures on the disk.
 *
 * @param disk The file pointer to the disk image.
 * @param file_name The name of the file to be created.
 * @param extension The extension of the file to be created.
 * @param permissions The permissions for the new file.
 * @param data The data to be written to the new file.
 * @param parent_inode_number The inode number of the parent directory where the file will be created.
 */
void create_file(FILE *disk, 
                 const char *file_name, 
                 const char *extension, 
                 uint32_t permissions,
                 const char *data,
                 uint32_t parent_inode_number) {

    printf("Creating file '%s.%s' in parent directory inode #%u\n", file_name, extension, parent_inode_number);

    // 1. Read necessary structures from disk
    // 1a. Read the group descriptor
    group_descriptor gd;
    fseek(disk, BLOCK_SIZE, SEEK_SET);
    fread(&gd, sizeof(group_descriptor), 1, disk);

    // 1b. Read the block bitmap
    uint8_t *block_bitmap = (uint8_t *)malloc(BLOCKS_COUNT / 8);
    fseek(disk, gd.block_bitmap * BLOCK_SIZE, SEEK_SET);
    fread(block_bitmap, BLOCKS_COUNT / 8, 1, disk);

    // 1c. Read the inode bitmap
    uint8_t *inode_bitmap = (uint8_t *)malloc(INODES_COUNT / 8);
    fseek(disk, gd.inode_bitmap * BLOCK_SIZE, SEEK_SET);
    fread(inode_bitmap, INODES_COUNT / 8, 1, disk);

    // 1d. Read the inode table
    inode_table itable;
    fseek(disk, gd.inode_table * BLOCK_SIZE, SEEK_SET);
    fread(&itable, sizeof(inode_table), 1, disk);

    printf("Finished step 1: reading structures from disk.\n");

    // 2. Create the file_t structure
    // 2a. Initialize the file_t structure
    size_t file_size = sizeof(file_t) + strlen(data);
    file_t *file_data = (file_t *)malloc(file_size);
    if (!file_data) {
        fprintf(stderr, "Error: could not allocate memory for file metadata\n");
        goto cleanup;
    }

    // Initialize the file_t structure
    strncpy(file_data->name, file_name, sizeof(file_data->name) - 1);
    strncpy(file_data->extension, extension, sizeof(file_data->extension) - 1);
    file_data->size = file_size;
    memcpy(file_data->data, data, strlen(data));

    // Allocate a new file inode
    inode *file_inode = allocate_inode(&itable, inode_bitmap, &gd, 0, permissions);
    if (!file_inode) {
        fprintf(stderr, "Error: cannot allocate inode for file\n");
        free(file_data);
        goto cleanup;
    }
    file_data->inode = file_inode->inode_number;

    // 2b. Set the inode
    file_inode->file_size = (uint32_t)file_size;
    file_inode->file_type = 0; // Regular file
    file_inode->permissions = permissions;

    // 2c. Calculate the number of blocks needed for the file
    size_t needed_blocks = (file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    printf("Finished step 2: created file metadata and allocated inode.\n");

    // 3. Add file to the parent directory's directory block
    // 3a. Read the parent directory block
    directory_block_t *parent_dir_block = read_directory(disk, parent_inode_number);
    if (!parent_dir_block) {
        fprintf(stderr, "Error: could not read parent directory block.\n");
        // Roll back the inode
        deallocate_inode(&itable, inode_bitmap, &gd, file_inode->inode_number);
        free(file_data);
        goto cleanup;
    }

    // 3b. Add the new file entry to the parent directory block
    if (parent_dir_block->entries_count >= BLOCK_SIZE / sizeof(dir_entry_t)) {
        fprintf(stderr, "Error: parent directory block is full.\n");
        // Roll back the inode
        deallocate_inode(&itable, inode_bitmap, &gd, file_inode->inode_number);
        free(file_data);
        free(parent_dir_block);
        goto cleanup;
    }

    char full_name[256];
    snprintf(full_name, sizeof(full_name), "%s.%s", file_name, extension);
    directory_block_t *new_parent_dir_block = add_entry_to_directory_block(parent_dir_block, file_inode->inode_number, full_name, 0);
    
    // 3c. Write the updated parent directory block back to disk
    update_directory(disk, &itable.inodes[parent_inode_number], block_bitmap, &gd, parent_inode_number, new_parent_dir_block);

    // 3d. Clean up the parent directory block
    free(parent_dir_block);
    free(new_parent_dir_block);

    printf("Finished step 3: added file entry to parent directory.\n");

    // 4. Allocate each needed block and write the file metadata/data
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
        size_t bytes_left = file_size - offset;
        size_t to_write = (bytes_left > BLOCK_SIZE) ? BLOCK_SIZE : bytes_left;

        fseek(disk, (FIRST_DATA_BLOCK + allocated_block) * BLOCK_SIZE, SEEK_SET);
        fwrite(src_ptr + offset, to_write, 1, disk);

        bytes_written += to_write;
    }

    free(file_data);

    printf("Finished step 4: allocated blocks and wrote file metadata/data.\n");

    // 5. Overwrite updated metadata structures
    // 5a. Group Descriptor
    fseek(disk, BLOCK_SIZE, SEEK_SET);
    fwrite(&gd, sizeof(gd), 1, disk);

    // 5b. Block Bitmap
    fseek(disk, gd.block_bitmap * BLOCK_SIZE, SEEK_SET);
    fwrite(block_bitmap, BLOCKS_COUNT / 8, 1, disk);

    // 5c. Inode Bitmap
    fseek(disk, gd.inode_bitmap * BLOCK_SIZE, SEEK_SET);
    fwrite(inode_bitmap, INODES_COUNT / 8, 1, disk);

    // 5d. Inode Table
    fseek(disk, gd.inode_table * BLOCK_SIZE, SEEK_SET);
    fwrite(&itable, sizeof(itable), 1, disk);

    printf("Finished step 5: updated metadata structures.\n");

    printf("File '%s.%s' created (inode #%u). Size=%lu bytes.\n", file_name, extension, file_inode->inode_number, file_size);

cleanup:
    free(block_bitmap);
    free(inode_bitmap);
    fseek(disk, 0, SEEK_SET);
}


/**
 * @brief Deletes a file from the file system.
 *
 * This function deletes a file by its inode number and updates the parent directory.
 * It performs the following steps:
 * 1. Reads necessary structures from the disk, including the group descriptor, block bitmap, inode bitmap, and inode table.
 * 2. Validates the inode number to ensure it is within a valid range and allocated.
 * 3. Frees all data blocks used by the file and deallocates the inode.
 * 4. Removes the file entry from the parent directory and updates the parent directory block on the disk.
 * 5. Writes the updated metadata back to the disk, including the group descriptor, block bitmap, inode bitmap, and inode table.
 *
 * @param disk A pointer to the file representing the disk.
 * @param inode_number The inode number of the file to be deleted.
 * @param parent_inode_number The inode number of the parent directory.
 */
void delete_file(FILE *disk, uint32_t inode_number, uint32_t parent_inode_number) {
    // 1. Read necessary structures from disk
    // 1a. Read the group descriptor
    group_descriptor gd;
    fseek(disk, BLOCK_SIZE, SEEK_SET);
    fread(&gd, sizeof(group_descriptor), 1, disk);

    // 1b. Read the block bitmap
    uint8_t *block_bitmap = (uint8_t *)malloc(BLOCKS_COUNT / 8);
    fseek(disk, gd.block_bitmap * BLOCK_SIZE, SEEK_SET);
    fread(block_bitmap, BLOCKS_COUNT / 8, 1, disk);

    // 1c. Read the inode bitmap
    uint8_t *inode_bitmap = (uint8_t *)malloc(INODES_COUNT / 8);
    fseek(disk, gd.inode_bitmap * BLOCK_SIZE, SEEK_SET);
    fread(inode_bitmap, INODES_COUNT / 8, 1, disk);

    // 1d. Read the inode table
    inode_table itable;
    fseek(disk, gd.inode_table * BLOCK_SIZE, SEEK_SET);
    fread(&itable, sizeof(inode_table), 1, disk);


    // 2. Validate inode number
    if (inode_number == 0 || inode_number > INODES_COUNT) {
        fprintf(stderr, "Error: invalid inode number %u\n", inode_number);
        goto cleanup;
    }

    inode *file_inode = &itable.inodes[inode_number];

    // Check if the inode is allocated
    if (is_bit_free(inode_bitmap, inode_number)) {
        fprintf(stderr, "Error: inode #%u is not allocated.\n", inode_number);
        goto cleanup;
    }

    // 3. Free all data blocks used by the file and deallocate the inode
    free_all_data_blocks_of_inode(disk, file_inode, block_bitmap, &gd);
    deallocate_inode(&itable, inode_bitmap, &gd, inode_number);

    // 4. Remove the file entry from the parent directory
    // 4a. Read the parent directory block
    directory_block_t *parent_dir_block = read_directory(disk, parent_inode_number);
    if (!parent_dir_block) {
        fprintf(stderr, "Error: could not read parent directory block.\n");
        goto cleanup;
    }

    // 4b. Write the updated parent directory block back to disk
    directory_block_t *new_parent_dir_block = remove_entry_from_directory_block(parent_dir_block, inode_number);
    update_directory(disk, &itable.inodes[parent_inode_number], block_bitmap, &gd, parent_inode_number, new_parent_dir_block);

    // 4c. Clean up the parent directory block
    free(parent_dir_block);
    free(new_parent_dir_block);

    // 5. Write updated metadata back to disk
    // 5a. Group Descriptor
    fseek(disk, BLOCK_SIZE, SEEK_SET);
    fwrite(&gd, sizeof(gd), 1, disk);

    // 5b. Block Bitmap
    fseek(disk, gd.block_bitmap * BLOCK_SIZE, SEEK_SET);
    fwrite(block_bitmap, BLOCKS_COUNT / 8, 1, disk);

    // 5c. Inode Bitmap
    fseek(disk, gd.inode_bitmap * BLOCK_SIZE, SEEK_SET);
    fwrite(inode_bitmap, INODES_COUNT / 8, 1, disk);

    // 5d. Inode Table
    fseek(disk, gd.inode_table * BLOCK_SIZE, SEEK_SET);
    fwrite(&itable, sizeof(itable), 1, disk);

    printf("File with inode #%u deleted successfully.\n", inode_number);

cleanup:
    free(block_bitmap);
    free(inode_bitmap);
}


// [CLI FUNCTIONS]
// Function to list directory contents
void list_directory_cli(const char *path) {
    // Not implemented yet
}

// Function to change directory
char* change_directory(const char *path) {
    // Not implemented yet
}

// Function to create a new directory
void make_directory_cli(const char *path) {
    // if (mkdir(path, 0755) != 0) {
    //     perror("mkdir");
    // }
    // Not implemented yet
}

// Function to remove a file or directory
void remove_entry_cli(const char *path) {
    // Not implemented yet
}

// [END CLI FUNCTIONS]

int main() {
    // Check if the drive file exists, if not, create it
    FILE *disk = fopen(DRIVE_NAME, "rb+");
    if (disk == NULL) {
        create_drive_file(DRIVE_NAME, BLOCK_SIZE * BLOCKS_COUNT);
        disk = fopen(DRIVE_NAME, "rb+");

        // Initialize the drive
        initialize_drive(disk);
    }

    create_file(disk, "test", "txt", 0644, "Hello, World!", 0);
    file_t* file = read_file(disk, 1);

    if (file != NULL) {
        printf("File Name: %s\n", file->name);
        printf("File Extension: %s\n", file->extension);
        printf("File Size: %lu bytes\n", (uint64_t)file->size);
        printf("File Inode: %u\n", file->inode);
        printf("File Data: %s\n", file->data);
    } else {
        printf("Error: Could not read file.\n");
    }

    // char input[MAX_INPUT_SIZE];

    // char *cwd = malloc(MAX_INPUT_SIZE);
    // cwd = change_directory(".");

    // while(1) {
    //     //Display the prompt
    //     printf("\033[1;32mcli_fi %s> \033[0m", cwd);
    //     fflush(stdout);

    //     // Read iinput
    //     if (fgets(input, MAX_INPUT_SIZE, stdin) == NULL) {
    //         printf("\n");
    //         break;
    //     }

    //     // Remove trailing newline
    //     input[strcspn(input, "\n")] = 0;

    //     // Skip empty input
    //     if (strlen(input) == 0) {
    //         continue;
    //     }

    //     // Parse input
    //     char *command = strtok(input, " ");
    //     char *arg = strtok(NULL, " ");

    //     // Execute command
    //     if (strcmp(command, "ls") == 0) {
    //         list_directory_cli(".");
    //     }
    //     else if (strcmp(command, "cd") == 0) {
    //         if (arg != NULL) {
    //             cwd = change_directory(arg);
    //         } else {
    //             printf("cd: missing argument\n");
    //         }
    //     }
    //     else if (strcmp(command, "mkdir") == 0) {
    //         if (arg != NULL) {
    //             make_directory_cli(arg);
    //         } else {
    //             printf("mkdir: missing argument\n");
    //         }
    //     }
    //     else if (strcmp(command, "rm") == 0) {
    //         if (arg != NULL) {
    //             remove_entry_cli(arg);
    //         } else {
    //             printf("rm: missing argument\n");
    //         }
    //     }
    //     else if (strcmp(command, "exit") == 0) {
    //         break;
    //     }
    //     else {
    //         printf("Unknown command: %s\n", command);
    //     }
    // }

    // printf("Exiting CLI.\n");
    // return 0;

    fclose(disk);

    return 0;
}