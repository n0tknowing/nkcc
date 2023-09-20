#include "cpp.h"

static void usage(int exit_code)
{
    puts("Usage:");
    puts("  cpp [-E] [-o OUT_FILE] FILE");
    puts("");
    puts("Options:");
    puts("  -o OUT_FILE     Place the output into OUT_FILE");
    puts("  -E              Preprocess only");
    exit(exit_code);
}

int main(int argc, char **argv)
{
    FILE *fp;
    cpp_context ctx;
    int opt, opt_e = 0;
    const char *in, *out = NULL;

    while ((opt = getopt(argc, argv, ":Eo:")) != EOF) {
        switch (opt) {
        case 'E':
            opt_e = 1;
            break;
        case 'o':
            out = optarg;
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
            return 1;
        }
    } else {
        fp = stdout;
    }

    in = argv[0];
    cpp_context_setup(&ctx);

    cpp_file *f = cpp_file_open(in, getenv("PWD"));
    if (f == NULL) {
        fprintf(stderr, "unable to open '%s': %s\n", in, strerror(errno));
        if (out != NULL) fclose(fp);
        cpp_context_cleanup(&ctx);
        return 1;
    }

    if (opt_e) {
        cpp_print(&ctx, f, fp);
    } else {
        cpp_run(&ctx, f);
        printf("total tokens: %u\n", ctx.ts.n);
    }

    if (out != NULL) fclose(fp);
    cpp_context_cleanup(&ctx);
}
