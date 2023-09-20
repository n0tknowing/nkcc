#include <assert.h>
#include <stdio.h>
#include <hash_table.h>
#include <string_pool.h>

#define L(s)  string_ref_newlen(s, sizeof(s) - 1)
int main(void)
{
    ht_t ht;

    char buf[16];
    string_ref keys[20];
    unsigned int vals[20] = {0};

    string_pool_setup();
    hash_table_setup(&ht, 16);

    for (int i = 0; i < 16; i++) {
        int len = snprintf(buf, sizeof(buf), "key_%02d", i);
        keys[i] = string_ref_newlen(buf, len);
        vals[i] = (unsigned int)i * 2;
        assert(hash_table_insert(&ht, keys[i], &vals[i]) == NULL);
    }

    assert(ht.count == 16);

    for (unsigned int i = 0; i < ht.capacity; i++) {
        ht_entry_t *ent = &ht.entries[i];
        if (ent->key == 0)
            continue;
        const char *key = string_ref_ptr(ent->key);
        const unsigned int val = *(const unsigned int *)ent->val;
        const uint64_t hash = ent->hash;
        const uint16_t psl = ent->psl;
        fprintf(stderr, "entry %02u: key=%s, val=%u, hash=0x%014lx, psl=%u\n",
                i, key, val, hash, psl);
    }

    for (int i = 0; i < 16; i++) {
        int len = snprintf(buf, sizeof(buf), "key_%02d", i);
        string_ref what = string_ref_newlen(buf, len);
        assert(hash_table_lookup(&ht, what) != NULL);
    }

    assert(hash_table_lookup(&ht, L("key_90")) == NULL);
    assert(hash_table_insert(&ht, keys[0], &vals[2]) != NULL);

    assert(hash_table_remove(&ht, keys[1]) != NULL);
    assert(ht.count == 15);

    fprintf(stderr, "--------------- after removal key_01 --------------\n");

    for (unsigned int i = 0; i < ht.capacity; i++) {
        ht_entry_t *ent = &ht.entries[i];
        if (ent->key == 0)
            continue;
        const char *key = string_ref_ptr(ent->key);
        const unsigned int val = *(const unsigned int *)ent->val;
        const uint64_t hash = ent->hash;
        const uint16_t psl = ent->psl;
        fprintf(stderr, "entry %02u: key=%s, val=%u, hash=0x%014lx, psl=%u\n",
                i, key, val, hash, psl);
    }

    assert(hash_table_lookup(&ht, L("key_00")) != NULL);
    assert(hash_table_lookup(&ht, L("key_01")) == NULL);

    assert(hash_table_insert(&ht, keys[1], &vals[1]) == NULL);
    assert(ht.count == 16);

    fprintf(stderr, "--------------- after reinsert key_01 --------------\n");

    for (unsigned int i = 0; i < ht.capacity; i++) {
        ht_entry_t *ent = &ht.entries[i];
        if (ent->key == 0)
            continue;
        const char *key = string_ref_ptr(ent->key);
        const unsigned int val = *(const unsigned int *)ent->val;
        const uint64_t hash = ent->hash;
        const uint16_t psl = ent->psl;
        fprintf(stderr, "entry %02u: key=%s, val=%u, hash=0x%014lx, psl=%u\n",
                i, key, val, hash, psl);
    }

    assert(hash_table_remove(&ht, L("key_13")) != NULL);
    assert(ht.count == 15);

    fprintf(stderr, "--------------- after removal key_13 --------------\n");

    for (unsigned int i = 0; i < ht.capacity; i++) {
        ht_entry_t *ent = &ht.entries[i];
        if (ent->key == 0)
            continue;
        const char *key = string_ref_ptr(ent->key);
        const unsigned int val = *(const unsigned int *)ent->val;
        const uint64_t hash = ent->hash;
        const uint16_t psl = ent->psl;
        fprintf(stderr, "entry %02u: key=%s, val=%u, hash=0x%014lx, psl=%u\n",
                i, key, val, hash, psl);
    }

    assert(hash_table_lookup(&ht, L("key_13")) == NULL);

    for (unsigned int i = 16; i < 20; i++) {
        int len = snprintf(buf, sizeof(buf), "key_%02d", i);
        keys[i] = string_ref_newlen(buf, len);
        vals[i] = (unsigned int)i * 2;
        assert(hash_table_insert(&ht, keys[i], &vals[i]) == NULL);
    }

    assert(ht.count == 19);

    fprintf(stderr, "--------------- after insertion new 4 entries --------------\n");

    for (unsigned int i = 0; i < ht.capacity; i++) {
        ht_entry_t *ent = &ht.entries[i];
        if (ent->key == 0)
            continue;
        const char *key = string_ref_ptr(ent->key);
        const unsigned int val = *(const unsigned int *)ent->val;
        const uint64_t hash = ent->hash;
        const uint16_t psl = ent->psl;
        fprintf(stderr, "entry %02u: key=%s, val=%u, hash=0x%014lx, psl=%u\n",
                i, key, val, hash, psl);
    }

    assert(hash_table_remove(&ht, L("key_09")) != NULL);
    assert(ht.count == 18);

    fprintf(stderr, "--------------- after removal key_09 --------------\n");

    for (unsigned int i = 0; i < ht.capacity; i++) {
        ht_entry_t *ent = &ht.entries[i];
        if (ent->key == 0)
            continue;
        const char *key = string_ref_ptr(ent->key);
        const unsigned int val = *(const unsigned int *)ent->val;
        const uint64_t hash = ent->hash;
        const uint16_t psl = ent->psl;
        fprintf(stderr, "entry %02u: key=%s, val=%u, hash=0x%014lx, psl=%u\n",
                i, key, val, hash, psl);
    }

    assert(hash_table_lookup(&ht, L("key_09")) == NULL);

    hash_table_clear(&ht);

    assert(ht.count == 0);
    assert(hash_table_lookup(&ht, L("key_00")) == NULL);

    hash_table_cleanup(&ht);
    string_pool_cleanup();
}
