#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "string_pool.h"

// block 0: remaining 0 bytes
// block 1: remaining 3890 bytes
// next probe start at 1
int main(void)
{
    string_ref refs[9];
    char buf[4000], buff[200], buffer[69], b[8000];

    string_pool_setup();

    refs[0] = string_ref_newlen("abc", 3); // block 0
    refs[1] = string_ref_newlen("def", 3); // block 0
    refs[2] = string_ref_newlen("abcdef", 6); // block 0
    refs[3] = string_ref_newlen("hello_world", 11); // block 0
    
    memset(buf, 'a', sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    refs[4] = string_ref_newlen(buf, sizeof(buf) - 1); // block 0

    memset(buff, 'b', sizeof(buff) - 1);
    buff[sizeof(buff) - 1] = 0;
    refs[5] = string_ref_newlen(buff, sizeof(buff) - 1); // block 1

    memset(buffer, 'c', sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = 0;
    refs[6] = string_ref_newlen(buffer, sizeof(buffer) - 1); // block 0
    refs[7] = string_ref_newlen("hello", 5); // block 1

    memset(b, 'd', sizeof(b) - 1);
    b[sizeof(b) - 1] = 0;
    refs[8] = string_ref_newlen(b, sizeof(b) - 1);

    (void)refs;
    string_pool_cleanup();
}
