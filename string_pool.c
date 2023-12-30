#define _GNU_SOURCE
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "string_pool.h"
#define XXH_INLINE_ALL
#include "xxhash.h"

#if defined(__GNUC__) || defined(__clang__)
#define likely(e)    __builtin_expect(!!(e), 1)
#define unlikely(e) __builtin_expect(!!(e), 0)
#else
#define likely(e)   e
#define unlikely(e) e
#endif

#define err_if(e, ...) do {                 \
        if (unlikely((e))) {                \
            fputs("fatal error: ", stderr); \
            fprintf(stderr, __VA_ARGS__);   \
            fputc('\n', stderr);            \
            exit(1);                        \
        }                                   \
    } while (0)

// ...
#define DEFAULT_POOL_CAPA   512u
#define DEFAULT_ARRAY_CAPA  512u
#define DEFAULT_BUFFER_SIZE (1u << 18) // 256KiB

// The pool table
static string_ref *g_pool;
static unsigned int g_count, g_capacity, g_load_factor;

// This is what a `string_ref` points to
struct string_off {
    unsigned int offset;
    unsigned int length;
};
static struct string_off *g_array;
static unsigned int g_array_count, g_array_capacity;

// The pool buffer
static char *g_buffer;
static unsigned int g_used, g_buffer_capacity;

static void __buffer_setup(void)
{
    g_buffer = mmap(NULL,
                    DEFAULT_BUFFER_SIZE,
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANON,
                    -1, 0);
    err_if(g_buffer == MAP_FAILED, "unable to allocate string pool: %s",
           strerror(errno));
    g_used = 1; // 0 is reserved for empty string
    g_buffer_capacity = DEFAULT_BUFFER_SIZE;
}

static void __buffer_cleanup(void)
{
    if (g_buffer)
        munmap(g_buffer, g_buffer_capacity);
}

static string_ref __buffer_new(const char *str, unsigned int __size)
{
    char *buffer;
    string_ref id = 0;
    unsigned int prev_size, size, capacity;

    buffer = g_buffer;
    prev_size = g_used;
    capacity = g_buffer_capacity;
    size = prev_size + __size + 1;

    if (size >= capacity) {
        capacity *= 2;
        buffer = mremap(g_buffer, g_buffer_capacity, capacity, MREMAP_MAYMOVE);
        err_if(g_buffer == MAP_FAILED, "unable to allocate string pool: %s",
               strerror(errno));
        g_buffer = buffer;
        g_buffer_capacity = capacity;
    }

    memcpy(buffer + prev_size, str, __size);
    g_used = size;

    if (g_array_count >= g_array_capacity) {
        capacity = g_array_capacity * 2;
        g_array = realloc(g_array, capacity * sizeof(*g_array));
        err_if(g_array == NULL, "unable to allocate string pool: %s",
               strerror(errno));
        memset(&g_array[g_array_count],
                0,
               (capacity - g_array_capacity) * sizeof(unsigned int));
        g_array_capacity = capacity;
    }

    id = g_array_count++;
    g_array[id].offset = prev_size;
    g_array[id].length = __size;
    return id;
}

static uint64_t __do_hash(const char *data, unsigned int len)
{
    return XXH3_64bits(data, len);
}

static string_ref __lookup(const char *s0, uint64_t hash, unsigned int len)
{
    const char *ptr;
    string_ref s, *strings = g_pool;
    unsigned int idx, mask = g_capacity - 1;

    idx = hash & mask;
    while ((s = strings[idx]) != 0) {
        ptr = g_buffer + g_array[s].offset;
        if (g_array[s].length == len && !memcmp(ptr, s0, len))
            return s;
        idx = (idx + 1) & mask;
    }

    return 0;
}

static void __try_resize(void)
{
    const char *ptr;
    string_ref *new_pool, *old_pool;
    unsigned int new_capacity, old_capacity, i, idx, mask;

    if (likely(g_count < g_load_factor))
        return;

    new_capacity = g_capacity * 2;
    mask = new_capacity - 1;
    new_pool = calloc(new_capacity, sizeof(string_ref));
    err_if(new_pool == NULL, "unable to allocate string pool: %s",
           strerror(errno));

    old_pool = g_pool;
    old_capacity = g_capacity;

    for (i = 0; i < old_capacity; i++) {
        string_ref s = old_pool[i];
        if (s != 0) {
            ptr = g_buffer + g_array[s].offset;
            idx = __do_hash(ptr, g_array[s].length) & mask;
            while (new_pool[idx] != 0)
                idx = (idx + 1) & mask;
            new_pool[idx] = s;
        }
    }

    free(old_pool);
    g_pool = new_pool;
    g_capacity = new_capacity;
    g_load_factor = (unsigned int)((double)new_capacity * 0.80);
}

void string_pool_setup(void)
{
    __buffer_setup();

    g_pool = calloc(DEFAULT_POOL_CAPA, sizeof(string_ref));
    err_if(g_pool == NULL, "unable to allocate string pool: %s",
           strerror(errno));
    g_count = 0;
    g_capacity = DEFAULT_POOL_CAPA;
    g_load_factor = (unsigned int)((double)(DEFAULT_POOL_CAPA * 0.80));

    g_array = calloc(DEFAULT_ARRAY_CAPA, sizeof(*g_array));
    err_if(g_array == NULL, "unable to allocate string pool: %s",
           strerror(errno));
    g_array_count = 1; // 0 is reserved for empty string
    g_array_capacity = DEFAULT_ARRAY_CAPA;
}

void string_pool_cleanup(void)
{
    __buffer_cleanup();

    free(g_pool);
    g_pool = NULL;

    free(g_array);
    g_array = NULL;
}

uint32_t string_pool_count(void)
{
    return g_count;
}

string_ref string_ref_newlen(const char *s, unsigned int len)
{
    string_ref str;
    unsigned int hash, idx, mask;

    if (unlikely(g_pool == NULL))
        string_pool_setup();

    if (len == 0)
        return 0;

    hash = __do_hash(s, len);
    str = __lookup(s, hash, len);
    if (str != 0)
        return str;

    __try_resize();

    mask = g_capacity - 1;
    idx = hash & mask;

    while (g_pool[idx] != 0)
        idx = (idx + 1) & mask;

    str = __buffer_new(s, len);
    g_pool[idx] = str;
    g_count++;

    return str;
}

string_ref string_ref_new(const char *s)
{
    return string_ref_newlen(s, strlen(s));
}

string_ref string_ref_concat(string_ref r0, string_ref r1)
{
    string_ref str;
    const char *ptr0, *ptr1;
    char buf[8192] = {0}, *ptr;
    unsigned int len, len0, len1;

    err_if(r0 >= g_array_capacity, "dangling string_ref (is 0x%08u)", r0);
    err_if(r1 >= g_array_capacity, "dangling string_ref (is 0x%08u)", r1);

    len0 = g_array[r0].length;
    ptr0 = g_buffer + g_array[r0].offset;

    len1 = g_array[r1].length;
    ptr1 = g_buffer + g_array[r1].offset;

    len = len0 + len1;
    if (len + 1 < sizeof(buf)) {
        memcpy(buf, ptr0, len0);
        memcpy(buf + len0, ptr1, len1);
        str = string_ref_newlen(buf, len); // '\0' added here
    } else {
        ptr = calloc(1, len);
        err_if(ptr == NULL, "unable to concat string_ref: %s", strerror(errno));
        memcpy(ptr, ptr0, len0);
        memcpy(ptr + len0, ptr1, len1);
        str = string_ref_newlen(ptr, len); // '\0' added here
        free(ptr);
    }

    return str;
}

const char *string_ref_ptr(string_ref r0)
{
    err_if(r0 >= g_array_capacity, "dangling string_ref (is 0x%08u)", r0);
    return g_buffer + g_array[r0].offset;
}

size_t string_ref_len(string_ref r0)
{
    err_if(r0 >= g_array_capacity, "dangling string_ref (is 0x%08u)", r0);
    return g_array[r0].length;
}

uint64_t string_ref_hash(string_ref r0)
{
    return __do_hash(string_ref_ptr(r0), string_ref_len(r0));
}
