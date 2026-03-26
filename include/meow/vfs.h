#pragma once

#include <stddef.h>
#include <stdint.h>
#include <meow/fat.h>

typedef int (*vfs_block_read_fn)(void* user, uint32_t lba, uint32_t count, void* buffer);

typedef struct VFS_BlockDeviceInfo {
    char name[16];
    uint32_t sector_size;
    uint32_t sector_count;
    uint8_t readable;
} VFS_BlockDeviceInfo;

void vfs_init(void);

int vfs_register_block_device(
    const char* name,
    uint32_t sector_size,
    uint32_t sector_count,
    vfs_block_read_fn read,
    void* read_user);

size_t vfs_block_device_count(void);
int vfs_get_block_device(size_t index, VFS_BlockDeviceInfo* out_info);

int vfs_mount_fat32_root(const char* device_name, uint32_t partition_lba);
int vfs_is_root_mounted(void);

int vfs_list_dir(const char* path, fat32_list_callback_fn callback, void* callback_user);
int vfs_read_file(const char* path, uint32_t offset, void* out_buffer, uint32_t bytes_to_read, uint32_t* out_bytes_read);
int vfs_read_block_device(const char* device_name, uint32_t lba, uint32_t count, void* out_buffer);

int vfs_file_open(const char* path);
int vfs_file_read(int handle, void* out_buffer, uint32_t bytes_to_read, uint32_t* out_bytes_read);
int vfs_file_seek(int handle, uint32_t offset);
int vfs_file_close(int handle);
