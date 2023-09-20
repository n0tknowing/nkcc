#include <assert.h>
#include <stdio.h>
#include <string_pool.h>
#include <hash_set.h>

int main(void)
{
    hset_t a;
    char buf[32];

    string_pool_setup();
    hset_setup(&a, 16);

    for (int i = 0; i < 32; i++) {
        int len = snprintf(buf, sizeof(buf), "mem_%02d", i);
        string_ref s = string_ref_newlen(buf, (unsigned int)len);
        hset_set(&a, s);
        assert(hset_find(&a, s));
    }

    for (int i = 0; i < 32; i++) {
        int len = snprintf(buf, sizeof(buf), "mem_%02d", i);
        string_ref s = string_ref_newlen(buf, (unsigned int)len);
        assert(hset_find(&a, s));
    }

    hset_cleanup(&a);
    string_pool_cleanup();
}
