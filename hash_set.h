#ifndef HASH_SET
#define HASH_SET

#include "string_pool.h"

typedef struct {
    string_ref *members;
    unsigned int count; // cardinality
    unsigned int capacity;
} hset_t;

// constructor and destructor
void hset_setup(hset_t *set, unsigned int capa);
void hset_cleanup(hset_t *set);

// empty the set without destruct it
void hset_clear(hset_t *set);

// insert `mem` into the set
void hset_set(hset_t *set, string_ref mem);

// remove `mem` from the set
void hset_remove(hset_t *set, string_ref mem);

// find `mem` in the set, return 1 if exist in the set, or 0 if not
int hset_find(hset_t *set, string_ref mem);

// create a new set `dst`, and copy all members from `src` into `dst`
void hset_copy(hset_t *dst, hset_t *src);

// do union on `src` and `dst` in-place, and store result into `dst`
void hset_union(hset_t *dst, hset_t *src);

// create a new set `dst`, do union on `src0` and `src1`, and store the result
// into `dst`
void hset_union_copy(hset_t *dst, hset_t *src0, hset_t *src1);

// do intersection on `src` and `dst` in-place, and store the result into `dst`.
void hset_intersection(hset_t *dst, hset_t *src);

// create a new set `dst`, do intersection on `src0` and `src1`, and store the
// result into `dst`
void hset_intersection_copy(hset_t *dst, hset_t *src0, hset_t *src1);

#endif
