#include <assert.h>
#include <stdio.h>
#include <hash_table.h>
#include <string_pool.h>

int main(void)
{
    ht_t ht;
    string_ref key_a, key_b, key_c;
    unsigned int val_a = 1;
    unsigned int val_b = 2;
    unsigned int val_c = 3;
    unsigned int capacity = 8;
    unsigned int mask = capacity - 1;

    string_pool_setup();
    hash_table_setup(&ht, capacity);

    key_a = string_ref_new("tombstone_a");

    for (unsigned int i = 0; ; i++) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "tombstone_b_%u", i);
        string_ref candidate = string_ref_newlen(buf, len);

        if ((string_ref_hash(key_a) & mask) ==
            (string_ref_hash(candidate) & mask)) {
            key_b = candidate;
            break;
        }
    }

    key_c = string_ref_new("tombstone_c");

    assert(hash_table_insert(&ht, key_a, &val_a) == NULL);
    assert(hash_table_insert(&ht, key_b, &val_b) == NULL);

    assert(hash_table_lookup(&ht, key_a) == &val_a);
    assert(hash_table_lookup(&ht, key_b) == &val_b);

    assert(hash_table_remove(&ht, key_a) == &val_a);
    assert(hash_table_lookup(&ht, key_a) == NULL);
    assert(hash_table_lookup(&ht, key_b) == &val_b);

    assert(hash_table_insert(&ht, key_c, &val_c) == NULL);
    assert(hash_table_lookup(&ht, key_b) == &val_b);
    assert(hash_table_lookup(&ht, key_c) == &val_c);

    assert(ht.count == 2);

    hash_table_cleanup(&ht);
    string_pool_cleanup();
}
