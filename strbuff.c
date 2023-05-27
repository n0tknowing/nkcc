#include <string.h>

#include "strbuff.h"

void strbuff_setup(strbuff_t *sb)
{
    memset(sb->buf, 0, sizeof(sb->buf));
    sb->max = 8191;
    sb->n = 0;
}

void strbuff_cleanup(strbuff_t *sb)
{
    memset(sb->buf, 0, sizeof(sb->buf));
    sb->max = sb->n = 0;
}

void strbuff_reset(strbuff_t *sb)
{
    strbuff_setup(sb);
}

void strbuff_append_char(strbuff_t *sb, char ch)
{
    if (sb->n < sb->max)
        sb->buf[sb->n++] = ch;
}

void strbuff_append_str(strbuff_t *sb, const char *str, int n)
{
    int remain = sb->max - sb->n;
    size_t to_copy = (size_t)(n > remain ? remain : n);
    memcpy(sb->buf + sb->n, str, to_copy);
    sb->n += (int)to_copy;
}

void strbuff_putstr(strbuff_t *sb, const char *str, int n)
{
    size_t to_copy = (size_t)(n > sb->max ? sb->max - 1 : n);
    memcpy(sb->buf, str, to_copy);
    sb->buf[to_copy] = 0;
    sb->n += (int)to_copy;
}
