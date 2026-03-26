#include <meow/fat.h>
#include <meow/string.h>

#define FAT32_EOC 0x0FFFFFF8u
#define FAT32_ATTR_DIRECTORY 0x10u
#define FAT32_ATTR_LONG_NAME 0x0Fu

static uint16_t rd16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int is_eoc(uint32_t cluster)
{
    return (cluster & 0x0FFFFFFFu) >= FAT32_EOC;
}

static uint32_t cluster_to_lba(const FAT32_FS* fs, uint32_t cluster)
{
    return fs->data_start_lba + ((cluster - 2u) * fs->sectors_per_cluster);
}

static int read_sector(const FAT32_FS* fs, uint32_t lba, void* out_sector)
{
    return fs->read_sectors(fs->read_user, lba, 1, out_sector);
}

static int fat32_next_cluster(FAT32_FS* fs, uint32_t cluster, uint32_t* out_next)
{
    uint32_t fat_offset = cluster * 4u;
    uint32_t sector_lba = fs->fat_start_lba + (fat_offset / fs->bytes_per_sector);
    uint32_t sector_off = fat_offset % fs->bytes_per_sector;

    if (read_sector(fs, sector_lba, fs->sector_scratch) != 0)
    {
        return -1;
    }

    if (sector_off + 4u > fs->bytes_per_sector)
    {
        return -1;
    }

    *out_next = rd32(&fs->sector_scratch[sector_off]) & 0x0FFFFFFFu;
    return 0;
}

static uint8_t up_ascii(uint8_t c)
{
    if (c >= 'a' && c <= 'z')
    {
        return (uint8_t)(c - ('a' - 'A'));
    }
    return c;
}

static int make_83_name(const char* segment, char out_name[11])
{
    for (int i = 0; i < 11; i++)
    {
        out_name[i] = ' ';
    }

    int i = 0;
    int j = 0;
    int ext = 0;

    while (segment[i] && segment[i] != '/')
    {
        uint8_t c = (uint8_t)segment[i];

        if (c == '.')
        {
            if (ext)
            {
                return -1;
            }
            ext = 1;
            j = 8;
            i++;
            continue;
        }

        c = up_ascii(c);

        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' || c == '~' || c == '-')
        {
            if ((!ext && j >= 8) || (ext && j >= 11))
            {
                return -1;
            }
            out_name[j++] = (char)c;
        }
        else
        {
            return -1;
        }

        i++;
    }

    return i;
}

static void trim_83_to_cstr(const uint8_t name[11], char out[13])
{
    int p = 0;

    for (int i = 0; i < 8 && name[i] != ' '; i++)
    {
        out[p++] = (char)name[i];
    }

    if (name[8] != ' ')
    {
        out[p++] = '.';
        for (int i = 8; i < 11 && name[i] != ' '; i++)
        {
            out[p++] = (char)name[i];
        }
    }

    out[p] = '\0';
}

static int find_in_directory(
    FAT32_FS* fs,
    uint32_t dir_cluster,
    const char* name83,
    FAT32_DirEntry* out_entry)
{
    uint32_t cluster = dir_cluster;

    while (!is_eoc(cluster) && cluster >= 2u)
    {
        uint32_t lba = cluster_to_lba(fs, cluster);

        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++)
        {
            if (read_sector(fs, lba + s, fs->sector_scratch) != 0)
            {
                return -1;
            }

            for (uint32_t off = 0; off < fs->bytes_per_sector; off += 32)
            {
                const uint8_t* e = &fs->sector_scratch[off];
                uint8_t first = e[0];
                uint8_t attr = e[11];

                if (first == 0x00)
                {
                    return 1;
                }
                if (first == 0xE5 || attr == FAT32_ATTR_LONG_NAME)
                {
                    continue;
                }

                if (memcmp((const void*)e, name83, 11) == 0)
                {
                    if (out_entry)
                    {
                        trim_83_to_cstr(e, out_entry->short_name);
                        out_entry->attr = attr;
                        out_entry->first_cluster = ((uint32_t)rd16(&e[20]) << 16) | rd16(&e[26]);
                        out_entry->size = rd32(&e[28]);
                    }
                    return 0;
                }
            }
        }

        if (fat32_next_cluster(fs, cluster, &cluster) != 0)
        {
            return -1;
        }
    }

    return 1;
}

static int resolve_path(FAT32_FS* fs, const char* path, FAT32_DirEntry* out_entry)
{
    if (!path || !*path)
    {
        return -1;
    }

    uint32_t current_cluster = fs->root_cluster;
    const char* p = path;

    if (*p == '/')
    {
        p++;
    }

    if (*p == '\0')
    {
        if (out_entry)
        {
            out_entry->short_name[0] = '/';
            out_entry->short_name[1] = '\0';
            out_entry->attr = FAT32_ATTR_DIRECTORY;
            out_entry->first_cluster = fs->root_cluster;
            out_entry->size = 0;
        }
        return 0;
    }

    while (*p)
    {
        char name83[11];
        int used = make_83_name(p, name83);
        FAT32_DirEntry entry;

        if (used <= 0)
        {
            return -1;
        }

        if (find_in_directory(fs, current_cluster, name83, &entry) != 0)
        {
            return -1;
        }

        p += used;

        if (*p == '/')
        {
            if ((entry.attr & FAT32_ATTR_DIRECTORY) == 0)
            {
                return -1;
            }
            current_cluster = entry.first_cluster;
            p++;
            continue;
        }

        if (*p != '\0')
        {
            return -1;
        }

        if (out_entry)
        {
            *out_entry = entry;
        }
        return 0;
    }

    return -1;
}

int fat32_mount(FAT32_FS* fs, fat32_read_sectors_fn read_sectors, void* read_user, uint32_t partition_lba)
{
    if (!fs || !read_sectors)
    {
        return -1;
    }

    memset(fs, 0, sizeof(*fs));
    fs->read_sectors = read_sectors;
    fs->read_user = read_user;
    fs->partition_lba = partition_lba;

    if (fs->read_sectors(fs->read_user, partition_lba, 1, fs->sector_scratch) != 0)
    {
        return -1;
    }

    if (fs->sector_scratch[510] != 0x55 || fs->sector_scratch[511] != 0xAA)
    {
        return -1;
    }

    fs->bytes_per_sector = rd16(&fs->sector_scratch[11]);
    fs->sectors_per_cluster = fs->sector_scratch[13];
    fs->reserved_sectors = rd16(&fs->sector_scratch[14]);
    fs->fat_count = fs->sector_scratch[16];

    uint16_t root_entry_count = rd16(&fs->sector_scratch[17]);
    uint16_t total16 = rd16(&fs->sector_scratch[19]);
    uint32_t total32 = rd32(&fs->sector_scratch[32]);
    uint16_t fat16_size = rd16(&fs->sector_scratch[22]);

    fs->sectors_per_fat = rd32(&fs->sector_scratch[36]);
    fs->root_cluster = rd32(&fs->sector_scratch[44]);

    if (fs->bytes_per_sector != 512 || fs->sectors_per_cluster == 0 || fs->fat_count == 0)
    {
        return -1;
    }

    if (root_entry_count != 0 || fat16_size != 0 || fs->sectors_per_fat == 0)
    {
        return -1;
    }

    uint32_t total_sectors = total16 ? total16 : total32;
    if (total_sectors == 0)
    {
        return -1;
    }

    fs->fat_start_lba = partition_lba + fs->reserved_sectors;
    fs->data_start_lba = fs->fat_start_lba + ((uint32_t)fs->fat_count * fs->sectors_per_fat);

    if (total_sectors <= (fs->reserved_sectors + (uint32_t)fs->fat_count * fs->sectors_per_fat))
    {
        return -1;
    }

    uint32_t data_sectors = total_sectors - (fs->reserved_sectors + (uint32_t)fs->fat_count * fs->sectors_per_fat);
    fs->total_clusters = data_sectors / fs->sectors_per_cluster;

    if (fs->total_clusters < 65525u)
    {
        return -1;
    }

    if (fs->root_cluster < 2u)
    {
        return -1;
    }

    return 0;
}

int fat32_open(FAT32_FS* fs, const char* path, FAT32_File* out_file)
{
    FAT32_DirEntry e;

    if (!fs || !path || !out_file)
    {
        return -1;
    }

    if (resolve_path(fs, path, &e) != 0)
    {
        return -1;
    }

    if (e.attr & FAT32_ATTR_DIRECTORY)
    {
        return -1;
    }

    out_file->fs = fs;
    out_file->first_cluster = e.first_cluster;
    out_file->size = e.size;
    out_file->position = 0;
    return 0;
}

int fat32_seek(FAT32_File* file, uint32_t offset)
{
    if (!file || !file->fs)
    {
        return -1;
    }

    if (offset > file->size)
    {
        return -1;
    }

    file->position = offset;
    return 0;
}

int fat32_read(FAT32_File* file, void* out_buffer, uint32_t bytes_to_read, uint32_t* out_bytes_read)
{
    FAT32_FS* fs;
    uint8_t* out;
    uint32_t remaining;
    uint32_t file_left;
    uint32_t cluster;
    uint32_t cluster_size;
    uint32_t clusters_to_skip;

    if (!file || !file->fs || !out_buffer)
    {
        return -1;
    }

    fs = file->fs;
    out = (uint8_t*)out_buffer;

    if (out_bytes_read)
    {
        *out_bytes_read = 0;
    }

    if (file->position >= file->size || bytes_to_read == 0)
    {
        return 0;
    }

    file_left = file->size - file->position;
    remaining = bytes_to_read < file_left ? bytes_to_read : file_left;

    cluster_size = (uint32_t)fs->bytes_per_sector * fs->sectors_per_cluster;
    cluster = file->first_cluster;

    if (cluster < 2u)
    {
        return -1;
    }

    clusters_to_skip = file->position / cluster_size;
    for (uint32_t i = 0; i < clusters_to_skip; i++)
    {
        if (fat32_next_cluster(fs, cluster, &cluster) != 0 || is_eoc(cluster))
        {
            return -1;
        }
    }

    uint32_t offset_in_cluster = file->position % cluster_size;

    while (remaining > 0 && !is_eoc(cluster))
    {
        uint32_t lba = cluster_to_lba(fs, cluster);

        for (uint32_t s = 0; s < fs->sectors_per_cluster && remaining > 0; s++)
        {
            uint32_t sector_base = s * fs->bytes_per_sector;

            if (offset_in_cluster >= sector_base + fs->bytes_per_sector)
            {
                continue;
            }

            if (read_sector(fs, lba + s, fs->sector_scratch) != 0)
            {
                return -1;
            }

            uint32_t off = 0;
            if (offset_in_cluster > sector_base)
            {
                off = offset_in_cluster - sector_base;
            }

            uint32_t can = fs->bytes_per_sector - off;
            if (can > remaining)
            {
                can = remaining;
            }

            memcpy(out, &fs->sector_scratch[off], can);
            out += can;
            remaining -= can;
            file->position += can;

            if (out_bytes_read)
            {
                *out_bytes_read += can;
            }
        }

        offset_in_cluster = 0;

        if (remaining > 0)
        {
            if (fat32_next_cluster(fs, cluster, &cluster) != 0)
            {
                return -1;
            }
        }
    }

    return 0;
}

int fat32_list_dir(FAT32_FS* fs, const char* path, fat32_list_callback_fn callback, void* callback_user)
{
    FAT32_DirEntry base;
    uint32_t cluster;

    if (!fs || !path || !callback)
    {
        return -1;
    }

    if (resolve_path(fs, path, &base) != 0)
    {
        return -1;
    }

    if ((base.attr & FAT32_ATTR_DIRECTORY) == 0)
    {
        return -1;
    }

    cluster = base.first_cluster;

    while (!is_eoc(cluster) && cluster >= 2u)
    {
        uint32_t lba = cluster_to_lba(fs, cluster);

        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++)
        {
            if (read_sector(fs, lba + s, fs->sector_scratch) != 0)
            {
                return -1;
            }

            for (uint32_t off = 0; off < fs->bytes_per_sector; off += 32)
            {
                FAT32_DirEntry e;
                const uint8_t* raw = &fs->sector_scratch[off];
                uint8_t first = raw[0];
                uint8_t attr = raw[11];

                if (first == 0x00)
                {
                    return 0;
                }
                if (first == 0xE5 || attr == FAT32_ATTR_LONG_NAME)
                {
                    continue;
                }

                trim_83_to_cstr(raw, e.short_name);
                e.attr = attr;
                e.first_cluster = ((uint32_t)rd16(&raw[20]) << 16) | rd16(&raw[26]);
                e.size = rd32(&raw[28]);

                if (callback(&e, callback_user) != 0)
                {
                    return 0;
                }
            }
        }

        if (fat32_next_cluster(fs, cluster, &cluster) != 0)
        {
            return -1;
        }
    }

    return 0;
}
