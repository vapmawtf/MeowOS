#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void write_padding(FILE* f, uint32_t bytes)
{
    static const uint8_t zero[4] = {0, 0, 0, 0};
    uint32_t pad = (4u - (bytes & 3u)) & 3u;
    if (pad)
    {
        (void)fwrite(zero, 1, pad, f);
    }
}

static int write_header(FILE* out,
                        const char* name,
                        uint32_t mode,
                        uint32_t filesize,
                        uint32_t ino)
{
    uint32_t namesz = (uint32_t)strlen(name) + 1u;
    char hdr[111];

    int n = snprintf(hdr,
                     sizeof(hdr),
                     "070701"
                     "%08x" /* ino */
                     "%08x" /* mode */
                     "%08x" /* uid */
                     "%08x" /* gid */
                     "%08x" /* nlink */
                     "%08x" /* mtime */
                     "%08x" /* filesize */
                     "%08x" /* devmajor */
                     "%08x" /* devminor */
                     "%08x" /* rdevmajor */
                     "%08x" /* rdevminor */
                     "%08x" /* namesize */
                     "%08x",/* check */
                     ino,
                     mode,
                     0u,
                     0u,
                     1u,
                     0u,
                     filesize,
                     0u,
                     0u,
                     0u,
                     0u,
                     namesz,
                     0u);

    if (n != 110)
    {
        return -1;
    }

    if (fwrite(hdr, 1, 110, out) != 110)
    {
        return -1;
    }

    if (fwrite(name, 1, namesz, out) != namesz)
    {
        return -1;
    }

    write_padding(out, 110u + namesz);
    return 0;
}

int main(int argc, char** argv)
{
    FILE* in;
    FILE* out;
    long in_size;
    uint8_t* buf;
    uint8_t* meowbox_buf = NULL;
    long meowbox_size = 0;

    if (argc != 3 && argc != 4)
    {
        fprintf(stderr, "usage: mkinitramfs <bin/sh> <out.cpio> [bin/meowbox]\n");
        return 1;
    }

    in = fopen(argv[1], "rb");
    if (!in)
    {
        perror("open input");
        return 1;
    }

    if (fseek(in, 0, SEEK_END) != 0)
    {
        fclose(in);
        return 1;
    }

    in_size = ftell(in);
    if (in_size < 0)
    {
        fclose(in);
        return 1;
    }

    if (fseek(in, 0, SEEK_SET) != 0)
    {
        fclose(in);
        return 1;
    }

    buf = (uint8_t*)malloc((size_t)in_size);
    if (!buf)
    {
        fclose(in);
        return 1;
    }

    if (fread(buf, 1, (size_t)in_size, in) != (size_t)in_size)
    {
        free(buf);
        fclose(in);
        return 1;
    }
    fclose(in);

    out = fopen(argv[2], "wb");
    if (!out)
    {
        perror("open output");
        free(buf);
        return 1;
    }

    if (write_header(out, "bin", 040755u, 0u, 1u) != 0)
    {
        fclose(out);
        free(buf);
        return 1;
    }

    if (write_header(out, "bin/sh", 0100755u, (uint32_t)in_size, 2u) != 0)
    {
        fclose(out);
        free(buf);
        return 1;
    }

    if (fwrite(buf, 1, (size_t)in_size, out) != (size_t)in_size)
    {
        fclose(out);
        free(buf);
        return 1;
    }
    write_padding(out, (uint32_t)in_size);

    if (argc == 4)
    {
        FILE* meowbox = fopen(argv[3], "rb");
        if (!meowbox)
        {
            perror("open meowbox");
            fclose(out);
            free(buf);
            return 1;
        }

        if (fseek(meowbox, 0, SEEK_END) != 0)
        {
            fclose(meowbox);
            fclose(out);
            free(buf);
            return 1;
        }

        meowbox_size = ftell(meowbox);
        if (meowbox_size < 0)
        {
            fclose(meowbox);
            fclose(out);
            free(buf);
            return 1;
        }

        if (fseek(meowbox, 0, SEEK_SET) != 0)
        {
            fclose(meowbox);
            fclose(out);
            free(buf);
            return 1;
        }

        meowbox_buf = (uint8_t*)malloc((size_t)meowbox_size);
        if (!meowbox_buf)
        {
            fclose(meowbox);
            fclose(out);
            free(buf);
            return 1;
        }

        if (fread(meowbox_buf, 1, (size_t)meowbox_size, meowbox) != (size_t)meowbox_size)
        {
            fclose(meowbox);
            fclose(out);
            free(meowbox_buf);
            free(buf);
            return 1;
        }
        fclose(meowbox);

        if (write_header(out, "bin/meowbox", 0100755u, (uint32_t)meowbox_size, 3u) != 0)
        {
            fclose(out);
            free(meowbox_buf);
            free(buf);
            return 1;
        }

        if (fwrite(meowbox_buf, 1, (size_t)meowbox_size, out) != (size_t)meowbox_size)
        {
            fclose(out);
            free(meowbox_buf);
            free(buf);
            return 1;
        }
        write_padding(out, (uint32_t)meowbox_size);
    }

    if (write_header(out, "TRAILER!!!", 0u, 0u, 4u) != 0)
    {
        fclose(out);
        free(meowbox_buf);
        free(buf);
        return 1;
    }

    fclose(out);
    free(meowbox_buf);
    free(buf);
    return 0;
}
