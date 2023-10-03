#include "cpp.h"

static void usage(int exit_code)
{
    puts("Usage:");
    puts("  cpp [-EP] [-D MACRO=VAL] [-I DIR] [-o OUT_FILE] [-U MACRO] FILE");
    puts("");
    puts("Options:");
    puts("  -D MACRO=VAL    Define MACRO to VAL (or 1 if VAL omitted)");
    puts("  -E              Preprocess only");
    puts("  -I DIR          Append DIR to the include search path");
    puts("  -P              Disable linemarker output in -E mode");
    puts("  -U MACRO        Undefine MACRO");
    puts("  -o OUT_FILE     Place the output into OUT_FILE");
    exit(exit_code);
}

int main(int argc, char **argv)
{
    int opt;
    FILE *fp;
    uchar opt_E;
    cpp_context ctx;
    const char *in, *out = NULL;

    opt_E = 0;
    cpp_context_setup(&ctx);

    while ((opt = getopt(argc, argv, ":D:EI:PU:o:")) != EOF) {
        switch (opt) {
        case 'D':
            cpp_macro_define(&ctx, optarg);
            break;
        case 'E':
            opt_E = 1;
            break;
        case 'I':
            cpp_search_path_append(&ctx, optarg);
            break;
        case 'P':
            break;
        case 'U':
            cpp_macro_undefine(&ctx, optarg);
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
        default:
            cpp_context_cleanup(&ctx);
            usage(1);
            break;
        }
    }

    argc -= optind;
    argv += optind;

    if (argc == 0) {
        cpp_context_cleanup(&ctx);
        usage(1);
    }

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

    cpp_file *f = cpp_file_open(in, in);
    if (f == NULL) {
        fprintf(stderr, "unable to open '%s': %s\n", in, strerror(errno));
        if (out != NULL) {
            fclose(fp);
            free((char *)out);
        }
        cpp_context_cleanup(&ctx);
        return 1;
    }

    if (opt_E) {
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
