#pragma once

#include <stddef.h>
#include <stdint.h>

typedef int (*fat32_read_sectors_fn)(void* user, uint32_t lba, uint32_t count, void* buffer);

typedef struct FAT32_FS {
    fat32_read_sectors_fn read_sectors;
    void* read_user;

    uint32_t partition_lba;

    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;

    uint32_t fat_start_lba;
    uint32_t data_start_lba;
    uint32_t total_clusters;

    uint8_t sector_scratch[512];
} FAT32_FS;

typedef struct FAT32_File {
    FAT32_FS* fs;
    uint32_t first_cluster;
    uint32_t size;
    uint32_t position;
} FAT32_File;

typedef struct FAT32_DirEntry {
    char short_name[13];
    uint8_t attr;
    uint32_t first_cluster;
    uint32_t size;
} FAT32_DirEntry;

typedef int (*fat32_list_callback_fn)(const FAT32_DirEntry* entry, void* user);

int fat32_mount(FAT32_FS* fs, fat32_read_sectors_fn read_sectors, void* read_user, uint32_t partition_lba);

int fat32_open(FAT32_FS* fs, const char* path, FAT32_File* out_file);
int fat32_read(FAT32_File* file, void* out_buffer, uint32_t bytes_to_read, uint32_t* out_bytes_read);
int fat32_seek(FAT32_File* file, uint32_t offset);

int fat32_list_dir(FAT32_FS* fs, const char* path, fat32_list_callback_fn callback, void* callback_user);
