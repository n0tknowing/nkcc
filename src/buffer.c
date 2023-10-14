#include "cpp.h"

void cpp_buffer_setup(cpp_buffer *buf, uint cap)
{
    uchar *data = mmap(NULL, CPP_BUFFER_MAX, PROT_READ|PROT_WRITE,
                       MAP_PRIVATE|MAP_ANON, -1, 0);
    if (unlikely(data == MAP_FAILED))
        cpp_error(NULL, NULL, "cpp_buffer fails to allocate %u bytes", cap);

    buf->data = data;
    buf->len = 0;
    buf->cap = cap;
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

    if (unlikely(buf->len + sizeof(uchar) >= buf->cap))
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

