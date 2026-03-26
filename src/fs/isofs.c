#include <meow/isofs.h>
#include <meow/string.h>

#define ISO_SECTOR_SIZE 2048u
#define ISO_PVD_SECTOR 16u
#define ISO_DIR_FLAG_DIRECTORY 0x02u

static uint32_t rd32le(const uint8_t* p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint8_t up_ascii(uint8_t c)
{
    if (c >= 'a' && c <= 'z')
    {
        return (uint8_t)(c - ('a' - 'A'));
    }
    return c;
}

static int names_equal_iso(const char* seg, size_t seg_len, const uint8_t* iso_name, uint8_t iso_len)
{
    size_t j = 0;

    while (j < iso_len && iso_name[j] != ';')
    {
        j++;
    }

    if (j != seg_len)
    {
        return 0;
    }

    for (size_t i = 0; i < seg_len; i++)
    {
        if (up_ascii((uint8_t)seg[i]) != up_ascii(iso_name[i]))
        {
            return 0;
        }
    }

    return 1;
}

static int parse_record_to_entry(const uint8_t* r, FAT32_DirEntry* out)
{
    uint8_t name_len = r[32];
    const uint8_t* name = &r[33];

    if (!out)
    {
        return -1;
    }

    if (name_len == 1 && (name[0] == 0 || name[0] == 1))
    {
        return 1;
    }

    size_t n = 0;
    while (n < name_len && name[n] != ';' && n < 12)
    {
        out->short_name[n] = (char)up_ascii(name[n]);
        n++;
    }
    out->short_name[n] = '\0';

    out->attr = (r[25] & ISO_DIR_FLAG_DIRECTORY) ? 0x10u : 0x20u;
    out->first_cluster = rd32le(&r[2]);
    out->size = rd32le(&r[10]);
    return 0;
}

static int find_in_directory(ISOFS_FS* fs, uint32_t extent, uint32_t size, const char* segment, size_t seg_len, FAT32_DirEntry* out)
{
    uint32_t remaining = size;
    uint32_t sector_index = 0;

    while (remaining > 0)
    {
        if (fs->read_sectors(fs->read_user, fs->partition_lba + extent + sector_index, 1, fs->sector_scratch) != 0)
        {
            return -1;
        }

        uint32_t off = 0;
        while (off < ISO_SECTOR_SIZE)
        {
            uint8_t len = fs->sector_scratch[off];
            if (len == 0)
            {
                break;
            }

            const uint8_t* r = &fs->sector_scratch[off];
            uint8_t name_len = r[32];
            const uint8_t* name = &r[33];

            if (!(name_len == 1 && (name[0] == 0 || name[0] == 1)) && names_equal_iso(segment, seg_len, name, name_len))
            {
                return parse_record_to_entry(r, out);
            }

            off += len;
        }

        if (remaining <= ISO_SECTOR_SIZE)
        {
            break;
        }

        remaining -= ISO_SECTOR_SIZE;
        sector_index++;
    }

    return 1;
}

static int resolve_path(ISOFS_FS* fs, const char* path, FAT32_DirEntry* out)
{
    FAT32_DirEntry cur;

    cur.attr = 0x10u;
    cur.first_cluster = fs->root_extent;
    cur.size = fs->root_size;
    cur.short_name[0] = '/';
    cur.short_name[1] = '\0';

    const char* p = path;
    if (!p || !*p)
    {
        return -1;
    }

    if (*p == '/')
    {
        p++;
    }

    if (*p == '\0')
    {
        if (out)
        {
            *out = cur;
        }
        return 0;
    }

    while (*p)
    {
        const char* seg = p;
        size_t seg_len = 0;
        FAT32_DirEntry next;

        while (*p && *p != '/')
        {
            seg_len++;
            p++;
        }

        if (seg_len == 0)
        {
            return -1;
        }

        if ((cur.attr & 0x10u) == 0)
        {
            return -1;
        }

        if (find_in_directory(fs, cur.first_cluster, cur.size, seg, seg_len, &next) != 0)
        {
            return -1;
        }

        cur = next;

        if (*p == '/')
        {
            p++;
        }
    }

    if (out)
    {
        *out = cur;
    }
    return 0;
}

int isofs_mount(ISOFS_FS* fs, isofs_read_sectors_fn read_sectors, void* read_user, uint32_t partition_lba)
{
    if (!fs || !read_sectors)
    {
        return -1;
    }

    memset(fs, 0, sizeof(*fs));
    fs->read_sectors = read_sectors;
    fs->read_user = read_user;
    fs->partition_lba = partition_lba;

    if (fs->read_sectors(fs->read_user, partition_lba + ISO_PVD_SECTOR, 1, fs->sector_scratch) != 0)
    {
        return -1;
    }

    if (fs->sector_scratch[0] != 1 || memcmp(&fs->sector_scratch[1], "CD001", 5) != 0)
    {
        return -1;
    }

    const uint8_t* root = &fs->sector_scratch[156];
    fs->root_extent = rd32le(&root[2]);
    fs->root_size = rd32le(&root[10]);

    if (fs->root_extent == 0 || fs->root_size == 0)
    {
        return -1;
    }

    return 0;
}

int isofs_list_dir(ISOFS_FS* fs, const char* path, fat32_list_callback_fn callback, void* callback_user)
{
    FAT32_DirEntry dir;
    uint32_t remaining;
    uint32_t sector_index = 0;

    if (!fs || !path || !callback)
    {
        return -1;
    }

    if (resolve_path(fs, path, &dir) != 0)
    {
        return -1;
    }

    if ((dir.attr & 0x10u) == 0)
    {
        return -1;
    }

    remaining = dir.size;
    while (remaining > 0)
    {
        if (fs->read_sectors(fs->read_user, fs->partition_lba + dir.first_cluster + sector_index, 1, fs->sector_scratch) != 0)
        {
            return -1;
        }

        uint32_t off = 0;
        while (off < ISO_SECTOR_SIZE)
        {
            uint8_t len = fs->sector_scratch[off];
            if (len == 0)
            {
                break;
            }

            FAT32_DirEntry e;
            int rc = parse_record_to_entry(&fs->sector_scratch[off], &e);
            if (rc == 0)
            {
                if (callback(&e, callback_user) != 0)
                {
                    return 0;
                }
            }

            off += len;
        }

        if (remaining <= ISO_SECTOR_SIZE)
        {
            break;
        }

        remaining -= ISO_SECTOR_SIZE;
        sector_index++;
    }

    return 0;
}

int isofs_open(ISOFS_FS* fs, const char* path, ISOFS_File* out_file)
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

    if (e.attr & 0x10u)
    {
        return -1;
    }

    out_file->fs = fs;
    out_file->extent = e.first_cluster;
    out_file->size = e.size;
    out_file->position = 0;
    return 0;
}

int isofs_seek(ISOFS_File* file, uint32_t offset)
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

int isofs_read(ISOFS_File* file, void* out_buffer, uint32_t bytes_to_read, uint32_t* out_bytes_read)
{
    uint8_t* out;
    uint32_t remaining;

    if (!file || !file->fs || !out_buffer)
    {
        return -1;
    }

    if (out_bytes_read)
    {
        *out_bytes_read = 0;
    }

    if (file->position >= file->size || bytes_to_read == 0)
    {
        return 0;
    }

    remaining = file->size - file->position;
    if (bytes_to_read < remaining)
    {
        remaining = bytes_to_read;
    }

    out = (uint8_t*)out_buffer;

    while (remaining > 0)
    {
        uint32_t abs_off = file->position;
        uint32_t sector_rel = abs_off / ISO_SECTOR_SIZE;
        uint32_t in_sector = abs_off % ISO_SECTOR_SIZE;
        uint32_t can = ISO_SECTOR_SIZE - in_sector;

        if (can > remaining)
        {
            can = remaining;
        }

        if (file->fs->read_sectors(file->fs->read_user,
                                   file->fs->partition_lba + file->extent + sector_rel,
                                   1,
                                   file->fs->sector_scratch) != 0)
        {
            return -1;
        }

        memcpy(out, &file->fs->sector_scratch[in_sector], can);
        out += can;
        file->position += can;
        remaining -= can;

        if (out_bytes_read)
        {
            *out_bytes_read += can;
        }
    }

    return 0;
}
