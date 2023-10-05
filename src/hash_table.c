#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hash_table.h"

#define TOMBSTONE ((void *)-1)

static unsigned int pow2_roundup(unsigned int x)
{
    x--;
    x |= x >> 1u;
    x |= x >> 2u;
    x |= x >> 4u;
    x |= x >> 8u;
    x |= x >> 16u;
    x++;
    return x;
}

void hash_table_setup(ht_t *ht, unsigned int capacity)
{
    ht_entry_t *entries;

    capacity = pow2_roundup(capacity);
    entries = calloc(capacity, sizeof(*entries));
    assert(entries != NULL);

    ht->entries = entries;
    ht->count = 0;
    ht->capacity = capacity;
    ht->load_factor = (unsigned int)((double)capacity * 0.80);
}

void hash_table_cleanup(ht_t *ht)
{
    free(ht->entries);
    memset(ht, 0, sizeof(*ht));
}

void hash_table_cleanup_with_free(ht_t *ht, void (*free_func)(void *))
{
    unsigned int i;
    ht_entry_t *entries = ht->entries;

    for (i = 0; i < ht->capacity; i++) {
        if (entries[i].key != 0 && entries[i].val != TOMBSTONE) {
            free_func(entries[i].val);
            entries[i].key = 0;
            entries[i].val = NULL;
            entries[i].hash = 0;
        }
    }

    free(entries);
    ht->entries = NULL;
}

void hash_table_clear(ht_t *ht)
{
    ht->count = 0;
    memset(ht->entries, 0, ht->capacity * sizeof(ht->entries[0]));
}

static void __try_resize(ht_t *ht)
{
    ht_entry_t *old_entries, *entries;
    unsigned int i, idx, old_capacity, capacity, mask;

    if (ht->count < ht->load_factor)
        return;

    capacity = ht->capacity * 2;
    entries = calloc(capacity, sizeof(*entries));
    assert(entries != NULL);

    old_entries = ht->entries;
    old_capacity = ht->capacity;
    mask = capacity - 1;

    for (i = 0; i < old_capacity; i++) {
        ht_entry_t *old_ent = &old_entries[i];
        if (old_ent->key == 0 || old_ent->val == TOMBSTONE)
            continue;
        idx = old_ent->hash & mask;
        while (entries[idx].key != 0)
            idx = (idx + 1) & mask;
        entries[idx] = *old_ent;
    }

    free(old_entries);
    ht->entries = entries;
    ht->capacity = capacity;
    ht->load_factor = (unsigned int)((double)capacity * 0.80);
}

static ht_entry_t *__do_lookup(ht_t *ht, string_ref key, uint64_t hash,
                               unsigned int *i)
{
    unsigned int idx, mask;

    mask = ht->capacity - 1;
    idx = hash & mask;

    while (ht->entries[idx].key != 0 && ht->entries[idx].val != TOMBSTONE) {
        if (ht->entries[idx].key == key && ht->entries[idx].hash == hash) {
            if (i) *i = idx;
            return &ht->entries[idx];
        }
        idx = (idx + 1) & mask;
    }

    return NULL;
}

void *hash_table_insert(ht_t *ht, string_ref key, void *val)
{
    uint64_t hash;
    unsigned int idx, mask;
    ht_entry_t *ent_at, ent;

    hash = string_ref_hash(key) & 0xfffffffffffful;
    ent_at = __do_lookup(ht, key, hash, NULL);

    if (ent_at != NULL) {
        void *old_val = ent_at->val;
        ent_at->val = val;
        return old_val;
    }

    __try_resize(ht);

    ent.key = key;
    ent.val = val;
    ent.hash = hash;

    mask = ht->capacity - 1;
    idx = hash & mask;
    ent_at = &ht->entries[idx];

    while (ent_at->key != 0 && ent_at->val != TOMBSTONE) {
        idx = (idx + 1) & mask;
        ent_at = &ht->entries[idx];
    }

    *ent_at = ent;
    ht->count++;
    return NULL;
}

void *hash_table_remove(ht_t *ht, string_ref key)
{
    void *ret_val = NULL;
    unsigned int mask = ht->capacity - 1;
    uint64_t hash = string_ref_hash(key) & 0xfffffffffffful;
    ht_entry_t *ent = __do_lookup(ht, key, hash, NULL), *prev;

    if (ent != NULL) {
        ret_val = ent->val;
        ent->val = TOMBSTONE;
        ht->count--;
    }

    return ret_val;
}

void *hash_table_lookup(ht_t *ht, string_ref key)
{
    uint64_t hash = string_ref_hash(key) & 0xffffffffffff;
    ht_entry_t *ent = __do_lookup(ht, key, hash, NULL);

    return ent ? ent->val : NULL;
}
