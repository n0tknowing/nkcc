#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "string_pool.h"

int main(void)
{
    char buf[16];
    string_ref refs[1024], copy[1024];

    string_pool_setup();

    for (int i = 0; i < 1024; i++) {
        int len = snprintf(buf, sizeof(buf), "buf%04d", i);
        refs[i] = string_ref_newlen(buf, (unsigned int)len);
    }

    for (int i = 0; i < 1024; i++) {
        int len = snprintf(buf, sizeof(buf), "buf%04d", i);
        copy[i] = string_ref_newlen(buf, (unsigned int)len);
        assert(refs[i] == copy[i]);
    }

    string_pool_cleanup();
}
