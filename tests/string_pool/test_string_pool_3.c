#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "string_pool.h"

int main(void)
{
    string_ref refs[8];
    char buf[3][4000];

    string_pool_setup();

    refs[0] = string_ref_newlen("abc", 3);
    refs[1] = string_ref_newlen("def", 3);
    
    memset(buf[0], 'a', sizeof(buf[0]) - 1);
    buf[0][sizeof(buf[0]) - 1] = 0;
    refs[2] = string_ref_newlen(buf[0], sizeof(buf[0]) - 1);

    memset(buf[1], 'b', sizeof(buf[1]) - 1);
    buf[1][sizeof(buf[1]) - 1] = 0;
    refs[3] = string_ref_newlen(buf[1], sizeof(buf[1]) - 1);

    memset(buf[2], 'c', sizeof(buf[2]) - 1);
    buf[2][sizeof(buf[2]) - 1] = 0;
    refs[4] = string_ref_newlen(buf[2], sizeof(buf[2]) - 1);

    refs[5] = string_ref_newlen("abcdef", 6);
    refs[6] = string_ref_newlen("xyz", 3);
    refs[7] = string_ref_newlen("abc", 3);

    (void)refs;
    string_pool_cleanup();
}
