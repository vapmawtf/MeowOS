#include <meow/vfs.h>
#include <meow/isofs.h>
#include <meow/string.h>

#define VFS_MAX_BLOCK_DEVICES 8

typedef struct VFS_BlockDevice {
    char name[16];
    uint32_t sector_size;
    uint32_t sector_count;
    vfs_block_read_fn read;
    vfs_block_write_fn write;
    void* read_user;
    uint8_t present;
} VFS_BlockDevice;

static VFS_BlockDevice g_devices[VFS_MAX_BLOCK_DEVICES];
static FAT32_FS g_root_fat;
static ISOFS_FS g_root_iso;
static uint8_t g_root_mounted;

enum {
    VFS_FS_NONE = 0,
    VFS_FS_FAT32 = 1,
    VFS_FS_ISO9660 = 2
};

static uint8_t g_root_fs_type;

typedef struct VFS_OpenFile {
    uint8_t used;
    uint8_t fs_type;
    union {
        FAT32_File fat;
        ISOFS_File iso;
    } file;
} VFS_OpenFile;

#define VFS_MAX_OPEN_FILES 16
static VFS_OpenFile g_open_files[VFS_MAX_OPEN_FILES];

typedef struct VFS_Link {
    uint8_t used;
    char alias[128];
    char target[128];
} VFS_Link;

#define VFS_MAX_LINKS 32
static VFS_Link g_links[VFS_MAX_LINKS];

static int vfs_read_bridge(void* user, uint32_t lba, uint32_t count, void* buffer);
static int vfs_write_bridge(void* user, uint32_t lba, uint32_t count, const void* buffer);

static int normalize_path(const char* in, char out[128]) {
    size_t o = 0;

    if (!in || !out) {
        return -1;
    }

    while (*in == ' ' || *in == '\t') {
        in++;
    }

    if (in[0] && in[1] == ':' && in[2] == '/') {
        in += 2;
    }

    if (*in == '\0') {
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }

    if (*in != '/') {
        out[o++] = '/';
    }

    while (*in && o + 1 < 128) {
        out[o++] = *in++;
    }
    out[o] = '\0';

    if (o == 0) {
        return -1;
    }

    while (o > 1 && out[o - 1] == '/') {
        out[o - 1] = '\0';
        o--;
    }

    return 0;
}

static int resolve_path_links(const char* in, char out[128]) {
    char tmp[128];

    if (normalize_path(in, tmp) != 0) {
        return -1;
    }

    for (int iter = 0; iter < 8; iter++) {
        int replaced = 0;
        for (size_t i = 0; i < VFS_MAX_LINKS; i++) {
            if (!g_links[i].used) {
                continue;
            }

            if (strcmp(tmp, g_links[i].alias) == 0) {
                strcpy(tmp, g_links[i].target);
                replaced = 1;
                break;
            }
        }

        if (!replaced) {
            break;
        }
    }

    strcpy(out, tmp);
    return 0;
}

static uint32_t rd32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64le(const uint8_t* p) {
    return (uint64_t)rd32le(p) | ((uint64_t)rd32le(p + 4) << 32);
}

static int try_mount_lba(VFS_BlockDevice* dev, uint32_t lba) {
    if (fat32_mount(&g_root_fat, vfs_read_bridge, vfs_write_bridge, dev, dev, lba) == 0) {
        g_root_mounted = 1;
        g_root_fs_type = VFS_FS_FAT32;
        return 0;
    }

    return -1;
}

static int probe_mbr_partitions(VFS_BlockDevice* dev) {
    if (vfs_read_bridge(dev, 0, 1, g_root_fat.sector_scratch) != 0) {
        return -1;
    }

    if (g_root_fat.sector_scratch[510] != 0x55 || g_root_fat.sector_scratch[511] != 0xAA) {
        return -1;
    }

    for (size_t i = 0; i < 4; i++) {
        const uint8_t* entry = &g_root_fat.sector_scratch[446 + (i * 16)];
        uint8_t type = entry[4];
        uint32_t start_lba = rd32le(&entry[8]);

        if (type == 0 || start_lba == 0) {
            continue;
        }

        if (try_mount_lba(dev, start_lba) == 0) {
            return 0;
        }
    }

    return -1;
}

static int probe_gpt_partitions(VFS_BlockDevice* dev) {
    uint8_t header[512];
    uint8_t entry_sector[512];

    if (vfs_read_bridge(dev, 1, 1, header) != 0) {
        return -1;
    }

    if (memcmp(header, "EFI PART", 8) != 0) {
        return -1;
    }

    uint64_t entries_lba64 = rd64le(&header[72]);
    uint32_t entry_count = rd32le(&header[80]);
    uint32_t entry_size = rd32le(&header[84]);

    if (entries_lba64 == 0 || entry_count == 0 || entry_size < 56 || entry_size > 512) {
        return -1;
    }

    uint32_t entries_lba = (uint32_t)entries_lba64;
    if ((uint64_t)entries_lba != entries_lba64) {
        return -1;
    }

    uint32_t max_entries = entry_count;
    if (max_entries > 128u) {
        max_entries = 128u;
    }

    for (uint32_t i = 0; i < max_entries; i++) {
        uint32_t byte_off = i * entry_size;
        uint32_t sector_off = byte_off / 512u;
        uint32_t in_sector = byte_off % 512u;
        const uint8_t* e;
        uint8_t type_empty = 1;

        if (in_sector + entry_size > 512u) {
            break;
        }

        if (vfs_read_bridge(dev, entries_lba + sector_off, 1, entry_sector) != 0) {
            return -1;
        }

        e = &entry_sector[in_sector];

        for (size_t b = 0; b < 16; b++) {
            if (e[b] != 0) {
                type_empty = 0;
                break;
            }
        }

        if (type_empty) {
            continue;
        }

        uint64_t first_lba64 = rd64le(&e[32]);
        if (first_lba64 == 0) {
            continue;
        }

        uint32_t first_lba = (uint32_t)first_lba64;
        if ((uint64_t)first_lba != first_lba64) {
            continue;
        }

        if (try_mount_lba(dev, first_lba) == 0) {
            return 0;
        }
    }

    return -1;
}

static int probe_common_lbas(VFS_BlockDevice* dev) {
    static const uint32_t common_lbas[] = { 2048u, 4096u, 8192u, 16384u, 32768u, 63u };

    for (size_t i = 0; i < sizeof(common_lbas) / sizeof(common_lbas[0]); i++) {
        if (try_mount_lba(dev, common_lbas[i]) == 0) {
            return 0;
        }
    }

    return -1;
}

static int probe_linear_lba_scan(VFS_BlockDevice* dev) {
    uint32_t limit = dev->sector_count;

    if (limit == 0 || limit > 262144u) {
        limit = 262144u;
    }

    for (uint32_t lba = 1; lba < limit; lba++) {
        if (try_mount_lba(dev, lba) == 0) {
            return 0;
        }
    }

    return -1;
}

static void copy_name(char dst[16], const char* src) {
    size_t i = 0;
    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (src[i] && i < 15) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static VFS_BlockDevice* find_device(const char* name) {
    for (size_t i = 0; i < VFS_MAX_BLOCK_DEVICES; i++) {
        if (!g_devices[i].present) {
            continue;
        }
        if (strcmp(g_devices[i].name, name) == 0) {
            return &g_devices[i];
        }
    }
    return 0;
}

static int vfs_read_bridge(void* user, uint32_t lba, uint32_t count, void* buffer) {
    VFS_BlockDevice* dev = (VFS_BlockDevice*)user;
    if (!dev || !dev->read) {
        return -1;
    }
    return dev->read(dev->read_user, lba, count, buffer);
}

static int vfs_write_bridge(void* user, uint32_t lba, uint32_t count, const void* buffer) {
    VFS_BlockDevice* dev = (VFS_BlockDevice*)user;
    if (!dev || !dev->write) {
        return -1;
    }
    return dev->write(dev->read_user, lba, count, buffer);
}

void vfs_init(void) {
    memset(g_devices, 0, sizeof(g_devices));
    memset(&g_root_fat, 0, sizeof(g_root_fat));
    memset(&g_root_iso, 0, sizeof(g_root_iso));
    memset(g_open_files, 0, sizeof(g_open_files));
    memset(g_links, 0, sizeof(g_links));
    g_root_mounted = 0;
    g_root_fs_type = VFS_FS_NONE;
}

int vfs_register_block_device(const char* name, uint32_t sector_size, uint32_t sector_count,
                              vfs_block_read_fn read, vfs_block_write_fn write, void* read_user) {
    if (!name || !name[0] || sector_size == 0) {
        return -1;
    }

    if (find_device(name)) {
        return -1;
    }

    for (size_t i = 0; i < VFS_MAX_BLOCK_DEVICES; i++) {
        if (!g_devices[i].present) {
            copy_name(g_devices[i].name, name);
            g_devices[i].sector_size = sector_size;
            g_devices[i].sector_count = sector_count;
            g_devices[i].read = read;
            g_devices[i].write = write;
            g_devices[i].read_user = read_user;
            g_devices[i].present = 1;
            return 0;
        }
    }

    return -1;
}

size_t vfs_block_device_count(void) {
    size_t count = 0;
    for (size_t i = 0; i < VFS_MAX_BLOCK_DEVICES; i++) {
        if (g_devices[i].present) {
            count++;
        }
    }
    return count;
}

int vfs_get_block_device(size_t index, VFS_BlockDeviceInfo* out_info) {
    size_t seen = 0;

    if (!out_info) {
        return -1;
    }

    for (size_t i = 0; i < VFS_MAX_BLOCK_DEVICES; i++) {
        if (!g_devices[i].present) {
            continue;
        }

        if (seen == index) {
            copy_name(out_info->name, g_devices[i].name);
            out_info->sector_size = g_devices[i].sector_size;
            out_info->sector_count = g_devices[i].sector_count;
            out_info->readable = g_devices[i].read ? 1u : 0u;
            out_info->writable = g_devices[i].write ? 1u : 0u;
            return 0;
        }

        seen++;
    }

    return -1;
}

int vfs_mount_fat32_root(const char* device_name, uint32_t partition_lba) {
    VFS_BlockDevice* dev;

    if (!device_name) {
        return -1;
    }

    dev = find_device(device_name);
    if (!dev || !dev->read) {
        return -1;
    }

    if (try_mount_lba(dev, partition_lba) != 0) {
        if (partition_lba != 0) {
            return -1;
        }

        if (probe_mbr_partitions(dev) == 0) {
            return 0;
        }

        if (probe_gpt_partitions(dev) == 0) {
            return 0;
        }

        if (probe_common_lbas(dev) == 0) {
            return 0;
        }

        if (probe_linear_lba_scan(dev) == 0) {
            return 0;
        }

        return -1;
    }

    return 0;
}

int vfs_mount_iso_root(const char* device_name, uint32_t partition_lba) {
    VFS_BlockDevice* dev;

    if (!device_name) {
        return -1;
    }

    dev = find_device(device_name);
    if (!dev || !dev->read) {
        return -1;
    }

    if (isofs_mount(&g_root_iso, vfs_read_bridge, dev, partition_lba) != 0) {
        return -1;
    }

    g_root_mounted = 1;
    g_root_fs_type = VFS_FS_ISO9660;
    return 0;
}

int vfs_is_root_mounted(void) {
    return g_root_mounted ? 1 : 0;
}

int vfs_list_dir(const char* path, fat32_list_callback_fn callback, void* callback_user) {
    char resolved[128];

    if (!g_root_mounted) {
        return -1;
    }

    if (resolve_path_links(path ? path : "/", resolved) != 0) {
        return -1;
    }

    if (g_root_fs_type == VFS_FS_FAT32) {
        return fat32_list_dir(&g_root_fat, resolved, callback, callback_user);
    }

    if (g_root_fs_type == VFS_FS_ISO9660) {
        return isofs_list_dir(&g_root_iso, resolved, callback, callback_user);
    }

    return -1;
}

int vfs_read_file(const char* path, uint32_t offset, void* out_buffer, uint32_t bytes_to_read,
                  uint32_t* out_bytes_read) {
    char resolved[128];

    if (!g_root_mounted || !path || !out_buffer) {
        return -1;
    }

    if (resolve_path_links(path, resolved) != 0) {
        return -1;
    }

    if (g_root_fs_type == VFS_FS_FAT32) {
        FAT32_File file;
        if (fat32_open(&g_root_fat, resolved, &file) != 0) {
            return -1;
        }

        if (fat32_seek(&file, offset) != 0) {
            return -1;
        }

        return fat32_read(&file, out_buffer, bytes_to_read, out_bytes_read);
    }

    if (g_root_fs_type == VFS_FS_ISO9660) {
        ISOFS_File file;
        if (isofs_open(&g_root_iso, resolved, &file) != 0) {
            return -1;
        }

        if (isofs_seek(&file, offset) != 0) {
            return -1;
        }

        return isofs_read(&file, out_buffer, bytes_to_read, out_bytes_read);
    }

    return -1;
}

int vfs_read_block_device(const char* device_name, uint32_t lba, uint32_t count, void* out_buffer) {
    VFS_BlockDevice* dev;

    if (!device_name || !out_buffer || count == 0) {
        return -1;
    }

    dev = find_device(device_name);
    if (!dev || !dev->read) {
        return -1;
    }

    if (dev->sector_count != 0 && (lba + count > dev->sector_count)) {
        return -1;
    }

    return dev->read(dev->read_user, lba, count, out_buffer);
}

int vfs_write_block_device(const char* device_name, uint32_t lba, uint32_t count,
                           const void* buffer) {
    VFS_BlockDevice* dev;

    if (!device_name || !buffer || count == 0) {
        return -1;
    }

    dev = find_device(device_name);
    if (!dev || !dev->write) {
        return -1;
    }

    if (dev->sector_count != 0 && (lba + count > dev->sector_count)) {
        return -1;
    }

    return dev->write(dev->read_user, lba, count, buffer);
}

int vfs_create_file(const char* path) {
    char resolved[128];

    if (!g_root_mounted || !path) {
        return -1;
    }
    if (g_root_fs_type != VFS_FS_FAT32) {
        return -1;
    }

    if (resolve_path_links(path, resolved) != 0) {
        return -1;
    }

    return fat32_create_file(&g_root_fat, resolved);
}

int vfs_write_file(const char* path, const void* data, uint32_t size) {
    char resolved[128];

    if (!g_root_mounted || !path || (!data && size != 0)) {
        return -1;
    }
    if (g_root_fs_type != VFS_FS_FAT32) {
        return -1;
    }

    if (resolve_path_links(path, resolved) != 0) {
        return -1;
    }

    return fat32_write_file(&g_root_fat, resolved, data, size);
}

int vfs_mkdir(const char* path) {
    char resolved[128];

    if (!g_root_mounted || !path) {
        return -1;
    }
    if (g_root_fs_type != VFS_FS_FAT32) {
        return -1;
    }

    if (resolve_path_links(path, resolved) != 0) {
        return -1;
    }

    return fat32_mkdir(&g_root_fat, resolved);
}

int vfs_link(const char* existing_path, const char* new_path) {
    char target[128];
    char alias[128];

    if (!g_root_mounted || !existing_path || !new_path) {
        return -1;
    }

    if (resolve_path_links(existing_path, target) != 0) {
        return -1;
    }

    if (normalize_path(new_path, alias) != 0) {
        return -1;
    }

    if (strcmp(alias, "/") == 0) {
        return -1;
    }

    if (g_root_fs_type == VFS_FS_FAT32) {
        FAT32_File file;
        if (fat32_open(&g_root_fat, target, &file) != 0) {
            return -1;
        }
    } else if (g_root_fs_type == VFS_FS_ISO9660) {
        ISOFS_File file;
        if (isofs_open(&g_root_iso, target, &file) != 0) {
            return -1;
        }
    } else {
        return -1;
    }

    for (size_t i = 0; i < VFS_MAX_LINKS; i++) {
        if (!g_links[i].used) {
            g_links[i].used = 1;
            strcpy(g_links[i].alias, alias);
            strcpy(g_links[i].target, target);
            return 0;
        }

        if (strcmp(g_links[i].alias, alias) == 0) {
            strcpy(g_links[i].target, target);
            return 0;
        }
    }

    return -1;
}

int vfs_file_open(const char* path) {
    char resolved[128];

    if (!g_root_mounted || !path) {
        return -1;
    }

    if (resolve_path_links(path, resolved) != 0) {
        return -1;
    }

    FAT32_File fat_file;
    ISOFS_File iso_file;
    uint8_t open_type = VFS_FS_NONE;

    if (g_root_fs_type == VFS_FS_FAT32) {
        if (fat32_open(&g_root_fat, resolved, &fat_file) != 0) {
            return -1;
        }
        open_type = VFS_FS_FAT32;
    } else if (g_root_fs_type == VFS_FS_ISO9660) {
        if (isofs_open(&g_root_iso, resolved, &iso_file) != 0) {
            return -1;
        }
        open_type = VFS_FS_ISO9660;
    } else {
        return -1;
    }

    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!g_open_files[i].used) {
            g_open_files[i].used = 1;
            g_open_files[i].fs_type = open_type;
            if (open_type == VFS_FS_FAT32) {
                g_open_files[i].file.fat = fat_file;
            } else {
                g_open_files[i].file.iso = iso_file;
            }
            return i;
        }
    }

    return -1;
}

int vfs_file_read(int handle, void* out_buffer, uint32_t bytes_to_read, uint32_t* out_bytes_read) {
    if (handle < 0 || handle >= VFS_MAX_OPEN_FILES || !out_buffer) {
        return -1;
    }

    if (!g_open_files[handle].used) {
        return -1;
    }

    if (g_open_files[handle].fs_type == VFS_FS_FAT32) {
        return fat32_read(&g_open_files[handle].file.fat, out_buffer, bytes_to_read,
                          out_bytes_read);
    }

    if (g_open_files[handle].fs_type == VFS_FS_ISO9660) {
        return isofs_read(&g_open_files[handle].file.iso, out_buffer, bytes_to_read,
                          out_bytes_read);
    }

    return -1;
}

int vfs_file_seek(int handle, uint32_t offset) {
    if (handle < 0 || handle >= VFS_MAX_OPEN_FILES) {
        return -1;
    }

    if (!g_open_files[handle].used) {
        return -1;
    }

    if (g_open_files[handle].fs_type == VFS_FS_FAT32) {
        return fat32_seek(&g_open_files[handle].file.fat, offset);
    }

    if (g_open_files[handle].fs_type == VFS_FS_ISO9660) {
        return isofs_seek(&g_open_files[handle].file.iso, offset);
    }

    return -1;
}

int vfs_file_close(int handle) {
    if (handle < 0 || handle >= VFS_MAX_OPEN_FILES) {
        return -1;
    }

    if (!g_open_files[handle].used) {
        return -1;
    }

    g_open_files[handle].used = 0;
    g_open_files[handle].fs_type = VFS_FS_NONE;
    memset(&g_open_files[handle].file, 0, sizeof(g_open_files[handle].file));
    return 0;
}
