#include "cpp.h"

static void usage(int exit_code)
{
    puts("Usage:");
    puts("  cpp [-EP] [-I DIR] [-o OUT_FILE] FILE");
    puts("");
    puts("Options:");
    puts("  -o OUT_FILE     Place the output into OUT_FILE");
    puts("  -E              Preprocess only");
    puts("  -I DIR          Append DIR to the include search path");
    puts("  -P              Disable linemarker output in -E mode");
    exit(exit_code);
}

int main(int argc, char **argv)
{
    FILE *fp;
    cpp_context ctx;
    int opt, opt_e = 0;
    const char *in, *out = NULL;

    cpp_context_setup(&ctx);

    while ((opt = getopt(argc, argv, ":EI:Po:")) != EOF) {
        switch (opt) {
        case 'E':
            opt_e = 1;
            break;
        case 'I':
            cpp_file_add_sysdir(optarg);
            break;
        case 'o':
            if (out != NULL) {
                fputs("error: -o is already specified\n", stderr);
                free((char *)out);
                cpp_context_cleanup(&ctx);
                return 1;
            }
            out = (const char *)strdup(optarg);
            assert(out);
            break;
        case 'P':
            break;
        default:
            usage(1);
            break;
        }
    }

    argc -= optind;
    argv += optind;

    if (argc == 0)
        usage(1);

    if (out != NULL) {
        fp = fopen(out, "w+");
        if (fp == NULL) {
            fprintf(stderr, "unable to open '%s': %s\n", out, strerror(errno));
            free((char *)out);
            cpp_context_cleanup(&ctx);
            return 1;
        }
    } else {
        fp = stdout;
    }

    in = argv[0];

    cpp_file *f = cpp_file_open(in, getenv("PWD"));
    if (f == NULL) {
        fprintf(stderr, "unable to open '%s': %s\n", in, strerror(errno));
        if (out != NULL) {
            fclose(fp);
            free((char *)out);
        }
        cpp_context_cleanup(&ctx);
        return 1;
    }

    if (opt_e) {
        cpp_print(&ctx, f, fp);
    } else {
        cpp_run(&ctx, f);
        printf("total tokens: %u\n", ctx.ts.n);
    }

    if (out != NULL) {
        fclose(fp);
        free((char *)out);
    }

    cpp_context_cleanup(&ctx);
}
