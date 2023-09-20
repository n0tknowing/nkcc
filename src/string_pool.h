#ifndef STRING_POOL_H
#define STRING_POOL_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t string_ref;

void string_pool_setup(void);
void string_pool_cleanup(void);
uint32_t string_pool_count(void);
string_ref string_ref_new(const char *);
string_ref string_ref_newlen(const char *, unsigned int);
string_ref string_ref_concat(string_ref, string_ref);
const char *string_ref_ptr(string_ref);
size_t string_ref_len(string_ref);
uint64_t string_ref_hash(string_ref);

#endif
