#include <meow/string.h>

// ---------------- Memory ----------------

void* memset(void* ptr, int value, size_t num)
{
    unsigned char* p = (unsigned char*)ptr;
    for (size_t i = 0; i < num; i++)
        p[i] = (unsigned char)value;
    return ptr;
}

void* memcpy(void* dest, const void* src, size_t num)
{
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < num; i++)
        d[i] = s[i];
    return dest;
}

void* memmove(void* dest, const void* src, size_t num)
{
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s)
    {
        for (size_t i = 0; i < num; i++)
            d[i] = s[i];
    }
    else
    {
        for (size_t i = num; i > 0; i--)
            d[i-1] = s[i-1];
    }
    return dest;
}

int memcmp(const void* ptr1, const void* ptr2, size_t num)
{
    const unsigned char* p1 = (const unsigned char*)ptr1;
    const unsigned char* p2 = (const unsigned char*)ptr2;
    for (size_t i = 0; i < num; i++)
    {
        if (p1[i] != p2[i])
            return p1[i] - p2[i];
    }
    return 0;
}

// ---------------- Strings ----------------

size_t strlen(const char* str)
{
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

char* strcpy(char* dest, const char* src)
{
    size_t i = 0;
    while (src[i])
    {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i]; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

char* strcat(char* dest, const char* src)
{
    size_t dest_len = strlen(dest);
    size_t i = 0;
    while (src[i])
    {
        dest[dest_len + i] = src[i];
        i++;
    }
    dest[dest_len + i] = '\0';
    return dest;
}

int strcmp(const char* str1, const char* str2)
{
    size_t i = 0;
    while (str1[i] && str1[i] == str2[i])
        i++;
    return (unsigned char)str1[i] - (unsigned char)str2[i];
}

int strncmp(const char* str1, const char* str2, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
    {
        if (str1[i] != str2[i] || !str1[i] || !str2[i])
            return (unsigned char)str1[i] - (unsigned char)str2[i];
    }
    return 0;
}

char* strchr(const char* str, int c)
{
    while (*str)
    {
        if (*str == (char)c)
            return (char*)str;
        str++;
    }
    return c == 0 ? (char*)str : NULL;
}

char* strrchr(const char* str, int c)
{
    char* last = NULL;
    while (*str)
    {
        if (*str == (char)c)
            last = (char*)str;
        str++;
    }
    return c == 0 ? (char*)str : last;
}