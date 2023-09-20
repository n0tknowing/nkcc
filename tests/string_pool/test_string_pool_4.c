#include <stdio.h>
#include <stdlib.h>
#include <string_pool.h>
#include <time.h>

static unsigned int do_hash(const char *data, unsigned int len)
{
    unsigned int hash = 0x811c9dc5u;
    const char *p = data, *e = data + len;

    while (p < e) {
        hash ^= *p++;
        hash *= 0x1000193u;
    }

    return hash;
}

int main(void)
{
    char buf[64];
    string_ref refs[16];

    string_pool_setup();
    srand(time(NULL));

    for (int i = 0; i < 16; i++) {
        int len = snprintf(buf, sizeof(buf), "buf%08x", rand());
        refs[i] = string_ref_newlen(buf, (unsigned int)len);
    }

    for (int i = 0; i < 16; i++) {
        const char *str = string_ref_ptr(refs[i]);
        unsigned int hash = do_hash(str, string_ref_len(refs[i]));
        printf("refs = 0x%08x, string = %s, ", refs[i], str);
        printf("hash = 0x%08x, index = %u\n", hash, hash & 127);
    }

    string_pool_cleanup();
}
