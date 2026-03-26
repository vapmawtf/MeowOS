#pragma once

#include <stdint.h>
#include <meow/fat.h>

typedef int (*isofs_read_sectors_fn)(void* user, uint32_t lba, uint32_t count, void* buffer);

typedef struct ISOFS_FS {
    isofs_read_sectors_fn read_sectors;
    void* read_user;
    uint32_t partition_lba;
    uint32_t root_extent;
    uint32_t root_size;
    uint8_t sector_scratch[2048];
} ISOFS_FS;

typedef struct ISOFS_File {
    ISOFS_FS* fs;
    uint32_t extent;
    uint32_t size;
    uint32_t position;
} ISOFS_File;

int isofs_mount(ISOFS_FS* fs, isofs_read_sectors_fn read_sectors, void* read_user, uint32_t partition_lba);
int isofs_list_dir(ISOFS_FS* fs, const char* path, fat32_list_callback_fn callback, void* callback_user);
int isofs_open(ISOFS_FS* fs, const char* path, ISOFS_File* out_file);
int isofs_seek(ISOFS_File* file, uint32_t offset);
int isofs_read(ISOFS_File* file, void* out_buffer, uint32_t bytes_to_read, uint32_t* out_bytes_read);
