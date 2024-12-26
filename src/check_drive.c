# include <stdio.h>
# include <stdlib.h>
# include <stdint.h>
# include "superblock.h"
# include "group_descriptor.h"
# include "bitmap.h"
# include "inode.h"

# define DRIVE_NAME "drive.bin"
# define BLOCK_SIZE 4096
# define BLOCKS_COUNT 32768
# define BLOCKS_PER_GROUP 16384

void check_superblock(FILE *file) {
    fseek(file, 0, SEEK_SET);

    superblock sb;
    fread(&sb, sizeof(superblock), 1, file);

    print_superblock(&sb);
}

void check_group_descriptor(FILE *file) {
    fseek(file, BLOCK_SIZE, SEEK_SET);

    group_descriptor gd;
    fread(&gd, sizeof(group_descriptor), 1, file);

    print_descriptor_block(&gd);
}

void check_bitmap(FILE *file, uint64_t block_offset, const char *label, uint32_t size) {
    fseek(file, block_offset * BLOCK_SIZE, SEEK_SET);

    uint8_t *bitmap = (uint8_t *)malloc(size / 8);
    fread(bitmap, size / 8, 1, file);

    printf("Bitmap for %s:\n", label);
    print_bitmap(bitmap, size);
}

void check_inode_table(FILE *file, uint32_t size) {
    fseek(file, 4 * BLOCK_SIZE, SEEK_SET);
    inode_table inode_table;
    fread(&inode_table, sizeof(inode_table), 1, file);

    fseek(file, 3 * BLOCK_SIZE, SEEK_SET);
    uint8_t *bitmap = (uint8_t *)malloc(size / 8);
    fread(bitmap, size / 8, 1, file);

    print_inode_table(&inode_table, bitmap);
}

int main() {
    FILE *file = fopen(DRIVE_NAME, "rb");
    if (file == NULL) {
        fprintf(stderr, "Error: Unable to open file %s\n", DRIVE_NAME);
        exit(EXIT_FAILURE);
    }

    check_superblock(file);                                     printf("\n");
    check_group_descriptor(file);                               printf("\n");
    check_bitmap(file, 2, "Data Block Bitmap", BLOCKS_COUNT);   printf("\n");
    check_bitmap(file, 3, "Inode Bitmap", INODES_COUNT);        printf("\n");
    check_inode_table(file, INODES_COUNT);                      printf("\n");

    fclose(file);
    return 0;
}