#include <stddef.h>
#include <stdint.h>
#include <meow/vga.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002
#define MULTIBOOT_INFO_FRAMEBUFFER (1u << 12)
#define DEFAULT_FONT_W 8u
#define DEFAULT_FONT_H 8u
#define FB_SCALE_X 1u
#define FB_SCALE_Y 2u

extern unsigned char drdos8x8_psfu_data[];
extern unsigned int drdos8x8_psfu_data_len;

typedef enum VideoBackend {
    VIDEO_BACKEND_TEXT,
    VIDEO_BACKEND_FRAMEBUFFER
} VideoBackend;

typedef struct PsfFont {
    const uint8_t* glyphs;
    const uint8_t* unicode_table;
    const uint8_t* end;
    uint32_t length;
    uint32_t charsize;
    uint32_t width;
    uint32_t height;
    uint8_t has_unicode;
    uint8_t valid;
} PsfFont;

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint8_t color_info[6];
} __attribute__((packed));

static VideoBackend backend = VIDEO_BACKEND_TEXT;
static size_t cursor_x;
static size_t cursor_y;

static uintptr_t fb_addr;
static uint32_t fb_pitch;
static uint32_t fb_width;
static uint32_t fb_height;
static uint8_t fb_bpp;

static PsfFont font = {
    .glyphs = 0,
    .unicode_table = 0,
    .end = 0,
    .length = 0,
    .charsize = 0,
    .width = DEFAULT_FONT_W,
    .height = DEFAULT_FONT_H,
    .has_unicode = 0,
    .valid = 0
};

static const uint8_t glyph_box[8] = {
    0x7E, 0x42, 0x5A, 0x5A, 0x5A, 0x5A, 0x42, 0x7E
};

static const uint8_t glyph_question[8] = {
    0x3C, 0x42, 0x02, 0x0C, 0x10, 0x00, 0x10, 0x00
};

static uint32_t psf_le32(const uint8_t* p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static void psf_try_load(void)
{
    const uint8_t* data = (const uint8_t*)drdos8x8_psfu_data;
    uint32_t len = (uint32_t)drdos8x8_psfu_data_len;

    font.valid = 0;

    if (len >= 4 && data[0] == 0x36 && data[1] == 0x04)
    {
        uint8_t mode = data[2];
        uint8_t charsize = data[3];
        uint32_t glyph_count = (mode & 0x01u) ? 512u : 256u;
        uint32_t glyph_bytes = glyph_count * (uint32_t)charsize;
        uint32_t glyph_off = 4u;

        if (glyph_off + glyph_bytes > len || charsize == 0)
        {
            return;
        }

        font.glyphs = data + glyph_off;
        font.unicode_table = (mode & 0x02u) ? (data + glyph_off + glyph_bytes) : 0;
        font.end = data + len;
        font.length = glyph_count;
        font.charsize = charsize;
        font.width = 8;
        font.height = charsize;
        font.has_unicode = (mode & 0x02u) ? 1u : 0u;
        font.valid = 1;
        return;
    }

    if (len >= 32 && psf_le32(data) == 0x864AB572u)
    {
        uint32_t header_size = psf_le32(data + 8);
        uint32_t flags = psf_le32(data + 12);
        uint32_t glyph_count = psf_le32(data + 16);
        uint32_t charsize = psf_le32(data + 20);
        uint32_t height = psf_le32(data + 24);
        uint32_t width = psf_le32(data + 28);
        uint32_t glyph_bytes = glyph_count * charsize;

        if (header_size > len || glyph_count == 0 || charsize == 0 || width == 0 || height == 0)
        {
            return;
        }

        if (header_size + glyph_bytes > len)
        {
            return;
        }

        font.glyphs = data + header_size;
        font.unicode_table = (flags & 0x01u) ? (data + header_size + glyph_bytes) : 0;
        font.end = data + len;
        font.length = glyph_count;
        font.charsize = charsize;
        font.width = width;
        font.height = height;
        font.has_unicode = (flags & 0x01u) ? 1u : 0u;
        font.valid = 1;
    }
}

static int is_utf8_cont(uint8_t b)
{
    return (b & 0xC0u) == 0x80u;
}

static uint32_t utf8_next(const char** s)
{
    const uint8_t* p = (const uint8_t*)*s;
    uint8_t b0 = p[0];

    if (b0 < 0x80)
    {
        *s += 1;
        return b0;
    }

    if ((b0 & 0xE0u) == 0xC0u)
    {
        uint8_t b1 = p[1];
        if (!b1 || !is_utf8_cont(b1))
        {
            *s += 1;
            return 0xFFFD;
        }
        *s += 2;
        return ((uint32_t)(b0 & 0x1Fu) << 6) | (uint32_t)(b1 & 0x3Fu);
    }

    if ((b0 & 0xF0u) == 0xE0u)
    {
        uint8_t b1 = p[1];
        uint8_t b2 = p[2];
        if (!b1 || !b2 || !is_utf8_cont(b1) || !is_utf8_cont(b2))
        {
            *s += 1;
            return 0xFFFD;
        }
        *s += 3;
        return ((uint32_t)(b0 & 0x0Fu) << 12)
             | ((uint32_t)(b1 & 0x3Fu) << 6)
             | (uint32_t)(b2 & 0x3Fu);
    }

    if ((b0 & 0xF8u) == 0xF0u)
    {
        uint8_t b1 = p[1];
        uint8_t b2 = p[2];
        uint8_t b3 = p[3];
        if (!b1 || !b2 || !b3 || !is_utf8_cont(b1) || !is_utf8_cont(b2) || !is_utf8_cont(b3))
        {
            *s += 1;
            return 0xFFFD;
        }
        *s += 4;
        return ((uint32_t)(b0 & 0x07u) << 18)
             | ((uint32_t)(b1 & 0x3Fu) << 12)
             | ((uint32_t)(b2 & 0x3Fu) << 6)
             | (uint32_t)(b3 & 0x3Fu);
    }

    *s += 1;
    return 0xFFFD;
}

static uint32_t utf8_next_raw(const uint8_t** p, const uint8_t* end)
{
    if (*p >= end)
    {
        return 0xFFFFFFFFu;
    }

    uint8_t b0 = *(*p)++;

    if (b0 < 0x80)
    {
        return b0;
    }

    if ((b0 & 0xE0u) == 0xC0u)
    {
        if (*p >= end || !is_utf8_cont(**p))
        {
            return 0xFFFFFFFFu;
        }
        uint8_t b1 = *(*p)++;
        return ((uint32_t)(b0 & 0x1Fu) << 6) | (uint32_t)(b1 & 0x3Fu);
    }

    if ((b0 & 0xF0u) == 0xE0u)
    {
        if ((end - *p) < 2 || !is_utf8_cont((*p)[0]) || !is_utf8_cont((*p)[1]))
        {
            return 0xFFFFFFFFu;
        }
        uint8_t b1 = *(*p)++;
        uint8_t b2 = *(*p)++;
        return ((uint32_t)(b0 & 0x0Fu) << 12)
             | ((uint32_t)(b1 & 0x3Fu) << 6)
             | (uint32_t)(b2 & 0x3Fu);
    }

    if ((b0 & 0xF8u) == 0xF0u)
    {
        if ((end - *p) < 3 || !is_utf8_cont((*p)[0]) || !is_utf8_cont((*p)[1]) || !is_utf8_cont((*p)[2]))
        {
            return 0xFFFFFFFFu;
        }
        uint8_t b1 = *(*p)++;
        uint8_t b2 = *(*p)++;
        uint8_t b3 = *(*p)++;
        return ((uint32_t)(b0 & 0x07u) << 18)
             | ((uint32_t)(b1 & 0x3Fu) << 12)
             | ((uint32_t)(b2 & 0x3Fu) << 6)
             | (uint32_t)(b3 & 0x3Fu);
    }

    return 0xFFFFFFFFu;
}

static const uint8_t* psf_glyph_by_index(uint32_t index)
{
    if (!font.valid || index >= font.length)
    {
        return 0;
    }
    return font.glyphs + index * font.charsize;
}

static const uint8_t* glyph_for_codepoint(uint32_t cp)
{
    const uint8_t* glyph = 0;

    if (font.valid && cp < font.length)
    {
        glyph = psf_glyph_by_index(cp);
        if (glyph)
        {
            return glyph;
        }
    }

    if (font.valid && font.has_unicode && font.unicode_table)
    {
        const uint8_t* p = font.unicode_table;
        for (uint32_t i = 0; i < font.length && p < font.end; i++)
        {
            while (p < font.end)
            {
                if (*p == 0xFF)
                {
                    p++;
                    break;
                }
                if (*p == 0xFE)
                {
                    p++;
                    continue;
                }

                const uint8_t* entry = p;
                uint32_t uc = utf8_next_raw(&entry, font.end);
                if (uc == cp)
                {
                    return psf_glyph_by_index(i);
                }

                if (entry == p)
                {
                    p++;
                }
                else
                {
                    p = entry;
                }
            }
        }
    }

    if (cp == ' ')
    {
        return 0;
    }

    if (font.valid)
    {
        return psf_glyph_by_index('?');
    }

    if (cp == 0xFFFD)
    {
        return glyph_box;
    }

    return glyph_question;
}

static inline void fb_put_pixel(uint32_t x, uint32_t y, uint32_t rgb)
{
    if (x >= fb_width || y >= fb_height)
    {
        return;
    }

    uint8_t* row = (uint8_t*)(fb_addr + (uintptr_t)y * fb_pitch);
    if (fb_bpp == 32)
    {
        uint32_t* px = (uint32_t*)(row + x * 4u);
        *px = rgb;
    }
    else if (fb_bpp == 24)
    {
        uint8_t* px = row + x * 3u;
        px[0] = (uint8_t)(rgb & 0xFFu);
        px[1] = (uint8_t)((rgb >> 8) & 0xFFu);
        px[2] = (uint8_t)((rgb >> 16) & 0xFFu);
    }
}

static uint32_t cell_w(void)
{
    uint32_t fw = font.valid ? font.width : DEFAULT_FONT_W;
    return (backend == VIDEO_BACKEND_FRAMEBUFFER) ? (fw * FB_SCALE_X) : fw;
}

static uint32_t cell_h(void)
{
    uint32_t fh = font.valid ? font.height : DEFAULT_FONT_H;
    return (backend == VIDEO_BACKEND_FRAMEBUFFER) ? (fh * FB_SCALE_Y) : fh;
}

static void fb_draw_glyph_cell(size_t cell_x, size_t cell_y, const uint8_t* glyph)
{
    uint32_t font_w = font.valid ? font.width : 8u;
    uint32_t font_h = font.valid ? font.height : 8u;
    uint32_t base_x = (uint32_t)(cell_x * cell_w());
    uint32_t base_y = (uint32_t)(cell_y * cell_h());
    uint32_t fg = 0xFFFFFFu;
    uint32_t bg = 0x000000u;
    uint32_t row_stride = (font.valid && font.height > 0) ? (font.charsize / font.height) : 1u;

    if (row_stride == 0)
    {
        row_stride = 1;
    }

    for (uint32_t y = 0; y < font_h; y++)
    {
        uint8_t bits = glyph ? glyph[y * row_stride] : 0;
        for (uint32_t x = 0; x < font_w; x++)
        {
            uint32_t color = (bits & (uint8_t)(1u << (7u - x))) ? fg : bg;
            uint32_t px0 = base_x + x * FB_SCALE_X;
            uint32_t py0 = base_y + y * FB_SCALE_Y;
            for (uint32_t sy = 0; sy < FB_SCALE_Y; sy++)
            {
                for (uint32_t sx = 0; sx < FB_SCALE_X; sx++)
                {
                    fb_put_pixel(px0 + sx, py0 + sy, color);
                }
            }
        }
    }
}

static size_t text_cols(void)
{
    if (backend == VIDEO_BACKEND_FRAMEBUFFER && fb_width >= DEFAULT_FONT_W)
    {
        uint32_t cw = cell_w();
        return cw ? (fb_width / cw) : WIDTH;
    }
    return WIDTH;
}

static size_t text_rows(void)
{
    if (backend == VIDEO_BACKEND_FRAMEBUFFER)
    {
        uint32_t ch = cell_h();
        return (ch && fb_height >= ch) ? (fb_height / ch) : HEIGHT;
    }
    return HEIGHT;
}

static void clear_line(size_t y)
{
    size_t cols = text_cols();
    if (backend == VIDEO_BACKEND_FRAMEBUFFER)
    {
        for (size_t x = 0; x < cols; x++)
        {
            fb_draw_glyph_cell(x, y, 0);
        }
    }
    else
    {
        volatile uint16_t* video = (volatile uint16_t*)VIDEO;
        for (size_t x = 0; x < cols; x++)
        {
            video[y * cols + x] = ((uint16_t)0x07 << 8) | ' ';
        }
    }
}

void vga_clear()
{
    size_t rows = text_rows();
    for (size_t y = 0; y < rows; y++)
    {
        clear_line(y);
    }
    cursor_x = 0;
    cursor_y = 0;
}

static void scroll()
{
    size_t cols = text_cols();
    size_t rows = text_rows();

    if (backend == VIDEO_BACKEND_FRAMEBUFFER)
    {
        size_t line_bytes = (size_t)fb_pitch * cell_h();
        size_t total_bytes = (size_t)fb_pitch * fb_height;
        uint8_t* fb = (uint8_t*)fb_addr;

        if (line_bytes < total_bytes)
        {
            size_t copy_bytes = total_bytes - line_bytes;
            for (size_t i = 0; i < copy_bytes; i++)
            {
                fb[i] = fb[i + line_bytes];
            }
            for (size_t i = copy_bytes; i < total_bytes; i++)
            {
                fb[i] = 0;
            }
        }
    }
    else
    {
        volatile uint16_t* video = (volatile uint16_t*)VIDEO;
        for (size_t y = 1; y < rows; y++)
        {
            for (size_t x = 0; x < cols; x++)
            {
                video[(y - 1) * cols + x] = video[y * cols + x];
            }
        }
    }

    clear_line(rows - 1);
}

static void put_codepoint(uint32_t cp)
{
    size_t cols = text_cols();
    size_t rows = text_rows();

    if (cp == '\n')
    {
        cursor_x = 0;
        cursor_y++;
    }
    else if (cp == '\b')
    {
        if (cursor_x > 0)
        {
            cursor_x--;
        }
        else if (cursor_y > 0)
        {
            cursor_y--;
            cursor_x = cols - 1;
        }

        if (backend == VIDEO_BACKEND_FRAMEBUFFER)
        {
            fb_draw_glyph_cell(cursor_x, cursor_y, 0);
        }
        else
        {
            volatile uint16_t* video = (volatile uint16_t*)VIDEO;
            video[cursor_y * cols + cursor_x] = ((uint16_t)0x07 << 8) | ' ';
        }
    }
    else
    {
        if (cursor_y < rows && cursor_x < cols)
        {
            if (backend == VIDEO_BACKEND_FRAMEBUFFER)
            {
                fb_draw_glyph_cell(cursor_x, cursor_y, glyph_for_codepoint(cp));
            }
            else
            {
                volatile uint16_t* video = (volatile uint16_t*)VIDEO;
                uint8_t ch = (cp < 128) ? (uint8_t)cp : (uint8_t)'?';
                video[cursor_y * cols + cursor_x] = ((uint16_t)0x07 << 8) | ch;
            }
        }

        cursor_x++;
        if (cursor_x >= cols)
        {
            cursor_x = 0;
            cursor_y++;
        }
    }

    if (cursor_y >= rows)
    {
        scroll();
        cursor_y = rows - 1;
    }
}

void vga_init(uint32_t multiboot_magic, uint32_t multiboot_info_addr)
{
    backend = VIDEO_BACKEND_TEXT;
    psf_try_load();

    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC)
    {
        return;
    }

    const struct multiboot_info* mbi = (const struct multiboot_info*)(uintptr_t)multiboot_info_addr;
    if ((mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER) == 0)
    {
        return;
    }

    if (mbi->framebuffer_type != 1)
    {
        return;
    }

    if (mbi->framebuffer_bpp != 24 && mbi->framebuffer_bpp != 32)
    {
        return;
    }

    fb_addr = (uintptr_t)mbi->framebuffer_addr;
    fb_pitch = mbi->framebuffer_pitch;
    fb_width = mbi->framebuffer_width;
    fb_height = mbi->framebuffer_height;
    fb_bpp = mbi->framebuffer_bpp;
    backend = VIDEO_BACKEND_FRAMEBUFFER;
}

void printstr(const char* str)
{
    while (*str)
    {
        uint32_t cp = utf8_next(&str);
        put_codepoint(cp);
    }
}