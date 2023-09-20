#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "string_pool.h"

#define L(s) string_ref_newlen(s, sizeof(s) - 1)

int main(void)
{
    char buf[16];
    string_ref string;

    string_pool_setup();

    string = L("Hello");
    string = string_ref_concat(string, L("World"));
    assert(string == L("HelloWorld"));

    string = string_ref_concat(string, L(""));
    assert(string == L("HelloWorld"));

    string_pool_cleanup();
}
