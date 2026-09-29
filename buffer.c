/*
 * buffer.c - scratch buffer
 *
 * Copyright (C) 2026  Widianto Nur F <xnaltasee@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "cpp.h"

void cpp_buffer_setup(cpp_buffer *buf, uint cap)
{
    size_t size = MIN(cap, CPP_BUFFER_MAX);
    uchar *data = mmap(NULL,
                       size,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANON,
                       -1,
                       0);
    if (unlikely(data == MAP_FAILED))
        cpp_error(NULL, NULL, "cpp_buffer fails to allocate %zu bytes", size);

    buf->data = data;
    buf->len = 0;
    buf->cap = size;
}

void cpp_buffer_cleanup(cpp_buffer *buf)
{
    if (buf->data != NULL) {
        munmap(buf->data, buf->cap);
        buf->data = NULL;
        buf->len = buf->cap = 0;
    }
}

void cpp_buffer_clear(cpp_buffer *buf)
{
    if (unlikely(buf->len == 0))
        return;

    memset(buf->data, 0, buf->len);
    buf->len = 0;
}

const uchar *cpp_buffer_append_ch(cpp_buffer *buf, uchar ch)
{
    const uchar *r = NULL;

    if (unlikely(buf->len + sizeof(ch) >= buf->cap))
        cpp_error(NULL, NULL, "cpp_buffer out of memory");
    else
        r = buf->data + buf->len;

    buf->data[buf->len++] = ch;
    return r;
}

const uchar *cpp_buffer_append(cpp_buffer *buf, const uchar *p, uint psize)
{
    const uchar *r = NULL;

    if (unlikely(psize == 1))
        return cpp_buffer_append_ch(buf, *p);
    else if (unlikely(buf->len + psize >= buf->cap))
        cpp_error(NULL, NULL, "cpp_buffer out of memory");
    else
        r = buf->data + buf->len;

    memcpy(buf->data + buf->len, p, psize);
    buf->len += psize;
    return r;
}

