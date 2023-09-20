//! gcc -O3 string_pool.c control.c -std=c11 -Wall -Wextra -o control

#include <stdio.h>
#include <string.h>

#include "string_pool.h"

// Intern pool API:
//
// Intern_pool new_intern_pool();
// void        del_intern_pool();
// uint        add(Intern_pool*, const char*);

int main(int argc, char* argv[])
{
    string_pool_setup();

    for (int i = 1; i < argc; i++)
    {
        FILE* file = fopen(argv[i], "r");
        if (file == 0)
        {
            perror("main");
            continue;
        }

        char buffer[256];
        while (fgets(buffer, 256, file)) { // one identifier per line
            buffer[strlen(buffer)-1] = '\0'; // removes the `\n` from fgets()
            string_ref_new(buffer);
        }

        fclose(file);
    }

    printf("%u\n", string_pool_count());
    string_pool_cleanup();
    return 0;
}
