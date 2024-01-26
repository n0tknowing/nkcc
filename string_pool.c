/*
 *  How the string pool (or string interning) works:
 *
 *  The implementation is very simple, each string_ref is actually an index to
 *  the `struct pool_array`, where index 0 is reserved for empty string.
 *  Its struct data, which is `struct string_off` stores information about a
 *  string_ref. The information needed are just the offset to the `struct
 *  pool_buffer` and the string length. Therefore, when one request a length
 *  of a string_ref, its time complexity is O(1).
 *
 *  Insertion is a bit costly, but that's expected. We're trading off insertion
 *  with lookup since in compiler, lookup is the most used operation to find
 *  duplicated identifier.
 *
 *  Note that string pool is only used for identifier, it's not for string
 *  literal, because it can be large. This is true for C, as string literals
 *  can be adjacent and they need to be concatenated after preprocessing stage
 *  and before syntax analysis stage.
 *
 *      const char *p = "Hello "
 *                      "world!"
 *                      "A very"
 *                      " long "
 *                      "string";
 *
 */

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

/* TODO: remove/replace this macro */
#define err_if(e, ...) do {                 \
        if (unlikely((e))) {                \
            fputs("fatal error: ", stderr); \
            fprintf(stderr, __VA_ARGS__);   \
            fputc('\n', stderr);            \
            exit(1);                        \
        }                                   \
    } while (0)

/* ... */
#define DEFAULT_POOL_CAPA   512u
#define DEFAULT_ARRAY_CAPA  512u
#define DEFAULT_BUFFER_SIZE (1u << 18) /* 256KiB */

/* The pool table.
 * Used to implement Set data structure to find duplicated string efficiently.
 * string_ref of 0 is never stored here.
 */
struct pool_table {
    string_ref *data;
    unsigned int count;
    unsigned int capacity;
    unsigned int load_factor;
};

/* This is what a `string_ref` points to. */
struct string_off {
    unsigned int offset; /* Offset in pool_buffer::data */
    unsigned int length; /* The length (excluding '\0') */
};

/* We store the information of each string_ref in an array for O(1) access.
 * There's no need to traverse the array. */
struct pool_array {
    struct string_off *data;
    unsigned int count;
    unsigned int capacity;
};

/* A giant mmap()-ed dynamic buffer to store the strings, contiguously.
 * Each string is guaranteed to be '\0'-terminated.
 */
struct pool_buffer {
    char *data;
    unsigned int count;
    unsigned int capacity;
};

static struct pool_table g_pool;
static struct pool_array g_array;
static struct pool_buffer g_buffer;

static void __buffer_setup(void)
{
    g_buffer.data = mmap(NULL,
                    DEFAULT_BUFFER_SIZE,
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANON,
                    -1, 0);
    err_if(g_buffer.data == MAP_FAILED, "unable to allocate string pool: %s",
           strerror(errno));
    g_buffer.count = 1; // 0 is reserved for empty string
    g_buffer.capacity = DEFAULT_BUFFER_SIZE;
}

static void __buffer_cleanup(void)
{
    if (g_buffer.data != NULL)
        munmap(g_buffer.data, g_buffer.capacity);
}

static string_ref __buffer_new(const char *str, unsigned int __size)
{
    char *buffer;
    string_ref id = 0;
    unsigned int prev_size, size, capacity;

    buffer = g_buffer.data;
    prev_size = g_buffer.count;
    capacity = g_buffer.capacity;
    size = prev_size + __size + 1;

    if (size >= capacity) {
        capacity *= 2;
        buffer = mremap(g_buffer.data, g_buffer.capacity, capacity, MREMAP_MAYMOVE);
        err_if(g_buffer.data == MAP_FAILED, "unable to allocate string pool: %s",
               strerror(errno));
        g_buffer.data = buffer;
        g_buffer.capacity = capacity;
    }

    memcpy(buffer + prev_size, str, __size);
    g_buffer.count = size;

    if (g_array.count >= g_array.capacity) {
        capacity = g_array.capacity * 2;
        g_array.data = realloc(g_array.data, capacity * sizeof(struct string_off));
        err_if(g_array.data == NULL, "unable to allocate string pool: %s",
               strerror(errno));
        memset(&g_array.data[g_array.count],
                0,
               (capacity - g_array.capacity) * sizeof(unsigned int));
        g_array.capacity = capacity;
    }

    id = g_array.count++;
    g_array.data[id].offset = prev_size;
    g_array.data[id].length = __size;
    return id;
}

static uint64_t __do_hash(const char *data, unsigned int len)
{
    return XXH3_64bits(data, len);
}

static string_ref __lookup(const char *s0, uint64_t hash, unsigned int len)
{
    const char *ptr;
    string_ref s, *strings = g_pool.data;
    unsigned int idx, mask = g_pool.capacity - 1;

    idx = hash & mask;
    while ((s = strings[idx]) != 0) {
        ptr = g_buffer.data + g_array.data[s].offset;
        if (g_array.data[s].length == len && !memcmp(ptr, s0, len))
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

    if (likely(g_pool.count < g_pool.load_factor))
        return;

    new_capacity = g_pool.capacity * 2;
    mask = new_capacity - 1;
    new_pool = calloc(new_capacity, sizeof(string_ref));
    err_if(new_pool == NULL, "unable to allocate string pool: %s",
           strerror(errno));

    old_pool = g_pool.data;
    old_capacity = g_pool.capacity;

    for (i = 0; i < old_capacity; i++) {
        string_ref s = old_pool[i];
        if (s != 0) {
            ptr = g_buffer.data + g_array.data[s].offset;
            idx = __do_hash(ptr, g_array.data[s].length) & mask;
            while (new_pool[idx] != 0)
                idx = (idx + 1) & mask;
            new_pool[idx] = s;
        }
    }

    free(old_pool);
    g_pool.data = new_pool;
    g_pool.capacity = new_capacity;
    g_pool.load_factor = (unsigned int)((double)new_capacity * 0.80);
}

void string_pool_setup(void)
{
    __buffer_setup();

    g_pool.data = calloc(DEFAULT_POOL_CAPA, sizeof(string_ref));
    err_if(g_pool.data == NULL, "unable to allocate string pool: %s",
           strerror(errno));
    g_pool.capacity = DEFAULT_POOL_CAPA;
    g_pool.load_factor = (unsigned int)((double)(DEFAULT_POOL_CAPA * 0.80));

    g_array.data = calloc(DEFAULT_ARRAY_CAPA, sizeof(struct string_off));
    err_if(g_array.data == NULL, "unable to allocate string pool: %s",
           strerror(errno));
    g_array.count = 1; // 0 is reserved for empty string
    g_array.capacity = DEFAULT_ARRAY_CAPA;
}

void string_pool_cleanup(void)
{
    __buffer_cleanup();

    free(g_pool.data);
    memset(&g_pool, 0, sizeof(g_pool));

    free(g_array.data);
    memset(&g_array, 0, sizeof(g_array));
}

uint32_t string_pool_count(void)
{
    return g_pool.count;
}

string_ref string_ref_newlen(const char *s, unsigned int len)
{
    string_ref str;
    unsigned int hash, idx, mask;

    if (unlikely(g_pool.data == NULL))
        string_pool_setup();

    if (len == 0)
        return 0;

    hash = __do_hash(s, len);
    str = __lookup(s, hash, len);
    if (str != 0)
        return str;

    __try_resize();

    mask = g_pool.capacity - 1;
    idx = hash & mask;

    while (g_pool.data[idx] != 0)
        idx = (idx + 1) & mask;

    str = __buffer_new(s, len);
    g_pool.data[idx] = str;
    g_pool.count++;

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

    err_if(r0 >= g_array.capacity, "dangling string_ref (is 0x%08u)", r0);
    err_if(r1 >= g_array.capacity, "dangling string_ref (is 0x%08u)", r1);

    len0 = g_array.data[r0].length;
    ptr0 = g_buffer.data + g_array.data[r0].offset;

    len1 = g_array.data[r1].length;
    ptr1 = g_buffer.data + g_array.data[r1].offset;

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
    err_if(r0 >= g_array.capacity, "0x%08u is not valid string_ref", r0);
    return g_buffer.data + g_array.data[r0].offset;
}

size_t string_ref_len(string_ref r0)
{
    err_if(r0 >= g_array.capacity, "0x%08u is not valid string_ref", r0);
    return g_array.data[r0].length;
}

uint64_t string_ref_hash(string_ref r0)
{
    return __do_hash(string_ref_ptr(r0), string_ref_len(r0));
}
