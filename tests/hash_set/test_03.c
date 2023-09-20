#include <assert.h>
#include <stdio.h>
#include <string_pool.h>
#include <hash_set.h>

#define L(s) string_ref_newlen(s, sizeof(s) - 1)

int main(void)
{
    hset_t a, b, c;

    string_pool_setup();
    hset_setup(&a, 8);
    hset_setup(&b, 8);

    // A = {1,3,5,7}
    hset_set(&a, L("1"));
    hset_set(&a, L("3"));
    hset_set(&a, L("5"));
    hset_set(&a, L("7"));

    // B = {1,2,4,6,7}
    hset_set(&b, L("1"));
    hset_set(&b, L("2"));
    hset_set(&b, L("4"));
    hset_set(&b, L("6"));
    hset_set(&b, L("7"));

    // C = A ∩ B
    hset_intersection_copy(&c, &a, &b);

    // C = {1,7}
    assert(hset_find(&c, L("1")));
    assert(hset_find(&c, L("7")));
    assert(!hset_find(&c, L("2")));
    assert(!hset_find(&c, L("3")));
    assert(!hset_find(&c, L("4")));
    assert(!hset_find(&c, L("5")));
    assert(!hset_find(&c, L("6")));

    hset_cleanup(&c);
    hset_cleanup(&b);
    hset_cleanup(&a);
    string_pool_cleanup();
}
