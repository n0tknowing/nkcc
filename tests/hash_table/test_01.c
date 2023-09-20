#include <assert.h>
#include <stdio.h>
#include <hash_table.h>
#include <string_pool.h>

int main(void)
{
    ht_t ht;

    char buf[16] = {0};
    unsigned int vals[4096] = {0};

    string_pool_setup();
    hash_table_setup(&ht, 8192);

    for (int i = 0; i < 4096; i++) {
        int len = snprintf(buf, sizeof(buf), "key_%02d", i);
        string_ref key = string_ref_newlen(buf, len);
        vals[i] = (unsigned int)i * 2;
        assert(hash_table_insert(&ht, key, &vals[i]) == NULL);
    }

    assert(ht.count == 4096);

    for (int i = 0; i < 4096; i++) {
        int len = snprintf(buf, sizeof(buf), "key_%02d", i);
        string_ref what = string_ref_newlen(buf, len);
        assert(hash_table_lookup(&ht, what) != NULL);
    }

    hash_table_cleanup(&ht);
    string_pool_cleanup();
}
