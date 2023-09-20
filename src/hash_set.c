// TODO: try to shrink the table after removal?
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hash_set.h"

#define TOMBSTONE       ((unsigned int)-1)
#define LOAD_FACTOR(_0) ((unsigned int)((double)((_0) * 0.75)))

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

void hset_setup(hset_t *set, unsigned int capacity)
{
    string_ref *members;

    capacity = pow2_roundup(capacity);
    members = calloc(capacity, sizeof(*members));
    assert(members != NULL);

    set->members = members;
    set->count = 0;
    set->capacity = capacity;
}

void hset_clear(hset_t *set)
{
    memset(set->members, 0, set->capacity * sizeof(set->members[0]));
    set->count = 0;
}

void hset_cleanup(hset_t *set)
{
    free(set->members);
    memset(set, 0, sizeof(*set));
}

static void __try_resize(hset_t *set)
{
    string_ref *old_members, *new_members;
    unsigned int i, j, mask, old_capacity, new_capacity, count;

    if (set->count < LOAD_FACTOR(set->capacity))
        return;

    old_capacity = set->capacity;
    new_capacity = old_capacity * 2;
    new_members = calloc(new_capacity, sizeof(*new_members));
    assert(new_members != NULL);

    count = 0;
    old_members = set->members;
    mask = new_capacity - 1;

    for (i = 0; i < old_capacity; i++) {
        if (old_members[i] != 0 && old_members[i] != TOMBSTONE) {
            j = string_ref_hash(old_members[i]) & mask;
            while (new_members[j] != 0)
                j = (j + 1) & mask;
            new_members[j] = old_members[i];
            count++;
        }
    }

    assert(count == set->count);

    free(old_members);
    set->members = new_members;
    set->capacity = new_capacity;
}

static int __find(hset_t *set, string_ref mem, uint64_t hash)
{
    string_ref *members = set->members;
    unsigned int idx, capacity = set->capacity, mask = capacity - 1;

    idx = hash & mask;
    while (members[idx] != 0) {
        if (members[idx] != TOMBSTONE && members[idx] == mem)
            return 1;
        idx = (idx + 1) & mask;
    }

    return 0;
}

void hset_set(hset_t *set, string_ref mem)
{
    uint64_t hash;
    string_ref *members;
    unsigned int idx, mask;

    hash = string_ref_hash(mem);
    if (__find(set, mem, hash))
        return;

    __try_resize(set);

    mask = set->capacity - 1;
    members = set->members;
    idx = hash & mask;
    while (members[idx] != 0 && members[idx] != TOMBSTONE)
        idx = (idx + 1) & mask;

    members[idx] = mem;
    set->count++;
}

void hset_remove(hset_t *set, string_ref mem)
{
    string_ref *members = set->members;
    uint64_t hash = string_ref_hash(mem);
    unsigned int idx, capacity = set->capacity, mask = capacity - 1;

    idx = hash & mask;
    while (members[idx] != 0) {
        if (members[idx] != TOMBSTONE && members[idx] == mem) {
            members[idx] = TOMBSTONE;
            set->count--;
            break;
        }
        idx = (idx + 1) & mask;
    }
}

int hset_find(hset_t *set, string_ref mem)
{
    return __find(set, mem, string_ref_hash(mem));
}

void hset_copy(hset_t *dst, hset_t *src)
{
    hset_setup(dst, src->capacity);
    dst->count = src->count;
    memcpy(dst->members, src->members, src->capacity * sizeof(string_ref));
}

void hset_union(hset_t *dst, hset_t *src)
{
    unsigned int i;

    for (i = 0; i < src->capacity; i++)
        hset_set(dst, src->members[i]);
}

void hset_union_copy(hset_t *dst, hset_t *src0, hset_t *src1)
{
    unsigned int i;
    hset_t *s, *d;

    if (src0->count > src1->count) {
        s = src0;
        d = src1;
    } else {
        s = src1;
        d = src0;
    }

    hset_copy(dst, s);

    for (i = 0; i < d->capacity; i++)
        hset_set(dst, d->members[i]);
}

void hset_intersection(hset_t *dst, hset_t *src)
{
    unsigned int i;

    for (i = 0; i < dst->capacity; i++) {
        string_ref m = dst->members[i];
        if (m && !hset_find(src, m))
            hset_remove(dst, m);
    }
}

void hset_intersection_copy(hset_t *dst, hset_t *src0, hset_t *src1)
{
    unsigned int i;
    hset_t *s, *d;

    if (src0->count < src1->count) {
        s = src0;
        d = src1;
    } else {
        s = src1;
        d = src0;
    }

    hset_setup(dst, s->count);

    for (i = 0; i < s->capacity; i++) {
        string_ref m = s->members[i];
        if (m && hset_find(d, m))
            hset_set(dst, m);
    }
}
