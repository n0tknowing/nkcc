#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include "string_pool.h"

typedef struct {
    string_ref key;
    void *val; // hash table don't own the value
    uint64_t hash : 48; // enough for use in compiler
    uint16_t psl;
} ht_entry_t;

typedef struct {
    ht_entry_t *entries;
    unsigned int count;
    unsigned int capacity;
    unsigned int load_factor;
} ht_t;

void hash_table_setup(ht_t *, unsigned int);
void hash_table_clear(ht_t *);
void hash_table_cleanup(ht_t *);
// since value is not owned by the hash table, this function exists
// to help freeing allocated value by user
void hash_table_cleanup_with_free(ht_t *, void (*free_func)(void *));
// insert or replace old val, NULL is returned if old val is not replaced
// otherwise old val is returned
void *hash_table_insert(ht_t *, string_ref, void *);
// associated val is returned
void *hash_table_remove(ht_t *, string_ref);
void *hash_table_lookup(ht_t *, string_ref);

#endif
