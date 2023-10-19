/* Current issues:
 * - Token spacing.
 * - Should not use fixed-size buffer when splicing a token.
 * - Too much assert() calls after allocation.
 * - Better memory allocation strategy for small structs such as macro_stack,
 *   cond_stack, cpp_stream, cpp_macro_arg, etc.
 * - Character constant inside a #if/#elif expression.
 * - Macro argument parsing doesn't work on rarer cases
 *   (Macro call inside macro arg: https://github.com/camel-cdr/bfcpp).
 *
 * Forever issues:
 * - Diagnostic.
 */
#include "cpp.h"

static void cpp_preprocess(cpp_context *ctx, cpp_token *tk);
static void cpp_stream_push(cpp_context *ctx, cpp_file *file);
static void cpp_stream_pop(cpp_context *ctx);
static void builtin_macro_setup(cpp_context *ctx);
static void predefined_macro_setup(cpp_context *ctx);
static void macro_stack_pop(cpp_context *ctx);
static void macro_stack_cleanup(cpp_context *ctx);
static void arg_stream_cleanup(cpp_context *ctx);
static void cond_stack_cleanup(cpp_context *ctx);
static void macro_free(void *p);
static cpp_token *expand_line(cpp_context *ctx, cpp_token *tk, uchar is_expr);
static uchar expand(cpp_context *ctx, cpp_token *tk, uchar is_expr);
static uint get_lineno_tok(cpp_context *ctx, cpp_token *tk);
static void skip_line(cpp_context *ctx, cpp_token *tk);
static void do_define(cpp_context *ctx, cpp_token *tk);
static void do_undef(cpp_context *ctx, cpp_token *tk);


/* ------------------------------------------------------------------------ */

static string_ref g_if,
                  g_ifdef,
                  g_ifndef,
                  g_elif,
                  g_else,
                  g_define,
                  g_include,
                  g_endif,
                  g_undef,
                  g_line,
                  g_pragma,
                  g_error;

static string_ref g__VA_ARGS__,
                  g__FILE__,
                  g__LINE__,
                  g__DATE__,
                  g__TIME__,
                  g__BASE_FILE__,
                  g__TIMESTAMP__,
                  g_defined;

static char *g_include_search_path[CPP_SEARCHPATH_MAX];
static int g_include_search_path_count;
static cond_expr g_cond_expr[CPP_CONDEXPR_MAX];
static int g_cond_expr_count;
static macro_stack_cache g_ms_cache;
static arg_stream_cache g_as_cache;

/* ------------------------------------------------------------------------ */

void cpp_context_setup(cpp_context *ctx)
{
    string_pool_setup();
    cpp_file_setup();

    g_if = LITREF("if");
    g_ifdef = LITREF("ifdef");
    g_ifndef = LITREF("ifndef");
    g_elif = LITREF("elif");
    g_else = LITREF("else");
    g_endif = LITREF("endif");
    g_include = LITREF("include");
    g_define = LITREF("define");
    g_undef = LITREF("undef");
    g_line = LITREF("line");
    g_pragma = LITREF("pragma");
    g_error = LITREF("error");

    g__VA_ARGS__ = LITREF("__VA_ARGS__");
    g__FILE__ = LITREF("__FILE__");
    g__LINE__ = LITREF("__LINE__");
    g__BASE_FILE__ = LITREF("__BASE_FILE__");
    g__TIMESTAMP__ = LITREF("__TIMESTAMP__");
    g__DATE__ = LITREF("__DATE__");
    g__TIME__ = LITREF("__TIME__");
    g_defined = LITREF("defined");

    memset(ctx, 0, sizeof(cpp_context));

    cpp_buffer_setup(&ctx->buf, CPP_BUFFER_MAX);

    cpp_search_path_append(ctx, "/usr/include");
    cpp_search_path_append(ctx, "/usr/local/include");
    cpp_search_path_append(ctx, "/usr/include/x86_64-linux-gnu");

    hash_table_setup(&ctx->cached_file, 16);
    hash_table_setup(&ctx->guarded_file, 32);
    hash_table_setup(&ctx->macro, 1024);

    cpp_token_array_setup(&ctx->temp, 4);
    cpp_token_array_setup(&ctx->line, 8);

    cpp_lex_setup(ctx);

    builtin_macro_setup(ctx);
    predefined_macro_setup(ctx);
}

void cpp_context_cleanup(cpp_context *ctx)
{
    int i;

    string_pool_cleanup();
    cpp_file_cleanup();
    cpp_lex_cleanup(ctx);

    while (ctx->stream != NULL) {
        cond_stack_cleanup(ctx);
        cpp_stream_pop(ctx);
    }

    arg_stream_cleanup(ctx);
    macro_stack_cleanup(ctx);
    cpp_buffer_cleanup(&ctx->buf);

    cpp_token_array_cleanup(&ctx->line);
    cpp_token_array_cleanup(&ctx->temp);
    cpp_token_array_cleanup(&ctx->ts);

    hash_table_cleanup(&ctx->cached_file);
    hash_table_cleanup(&ctx->guarded_file);
    hash_table_cleanup_with_free(&ctx->macro, macro_free);

    for (i = 0; i < g_include_search_path_count; i++) {
        free(g_include_search_path[i]);
        g_include_search_path[i] = NULL;
    }

    g_include_search_path_count = 0;
    memset(ctx, 0, sizeof(cpp_context));
}

void cpp_search_path_append(cpp_context *ctx, const char *dirpath)
{
    char *path;

    if (g_include_search_path_count == CPP_SEARCHPATH_MAX)
        cpp_error(ctx, NULL, "too many #include search paths");

    path = strdup(dirpath);
    assert(path);
    g_include_search_path[g_include_search_path_count] = path;
    g_include_search_path_count++;
}

void cpp_run(cpp_context *ctx, cpp_file *file)
{
    cpp_token tk;

    cpp_token_array_setup(&ctx->ts, 8192);
    cpp_stream_push(ctx, file);

    while (1) {
        cpp_preprocess(ctx, &tk);
        if (tk.kind == TK_eof)
            break;
        cpp_token_array_append(&ctx->ts, &tk);
    }

    cpp_token_array_append(&ctx->ts, &tk); /* TK_eof */
}

void cpp_print(cpp_context *ctx, cpp_file *file, FILE *fp)
{
    cpp_token tk;
    uchar first = 1;
    cpp_stream_push(ctx, file);

    while (1) {
        cpp_preprocess(ctx, &tk);
        if (tk.kind == TK_eof)
            break;
        if (AT_BOL(&tk) && !first)
            fputc('\n', fp);
        cpp_token_print(fp, &tk);
        first = 0;
    }

    if (!first)
        fputc('\n', fp);
}

void cpp_dump_token(cpp_context *ctx, FILE *fp)
{
    uint i, len;
    cpp_token *tk;
    uchar buf[1024];
    uchar at_bof, at_bol, has_spc;
    const char *tk_kind, *p, *file;

    if (unlikely(ctx->ts.max == 0))
        cpp_error(ctx, NULL, "please call cpp_run first before calling "
                             "cpp_dump_token");

    for (i = 0; i < ctx->ts.n; i++) {
        tk = &ctx->ts.tokens[i];
        at_bof = HAS_FLAG(tk->flags, CPP_TOKEN_BOF);
        at_bol = HAS_FLAG(tk->flags, CPP_TOKEN_BOL);
        has_spc = HAS_FLAG(tk->flags, CPP_TOKEN_SPACE);
        tk_kind = cpp_token_kind(tk->kind);
        file = string_ref_ptr(cpp_file_no(tk->fileno)->name);
        len = tk->length;
        if (tk->kind == TK_identifier) {
            p = string_ref_ptr(tk->p.ref);
        } else {
            len = cpp_token_splice(tk, buf, sizeof(buf));
            p = (const char *)buf;
        }
        fprintf(fp, "[%c%c%c] %s, '%.*s', Loc=<%s:%u>\n",
                     at_bof ? 'F' : 'f',
                     at_bol ? 'L' : 'l',
                     has_spc ? 'S' : 's',
                     tk_kind,
                     len, p,
                     file, tk->lineno);
    }

    puts("\nToken flags:");
    puts("  'F' -- Beginning of file");
    puts("  'f' -- Not beginning of file");
    puts("  'L' -- Beginning of line");
    puts("  'l' -- Not beginning of line");
    puts("  'S' -- Token followed by whitespace");
    puts("  's' -- Token not followed by whitespace");
    printf("Token count: %u\n", ctx->ts.n);
}

void cpp_macro_define(cpp_context *ctx, const char *in)
{
    cpp_token tk;
    cpp_stream s;
    const char *p;
    uint len1, len2;
    const uchar *sp;
    cpp_file *f = cpp_file_no(0);

    len1 = strlen(in);
    p = memchr(in, '=', len1);
    if (p != NULL) { /* replace the first '=' to a single whitespace */
        len2 = (uint)(p - in);
        sp = cpp_buffer_append(&ctx->buf, (const uchar *)in, len2);
        cpp_buffer_append_ch(&ctx->buf, ' ');
        p++; len2 = (uint)(in + len1 - p);
        cpp_buffer_append(&ctx->buf, (const uchar *)p, len2);
    } else { /* append " 1" */
        sp = cpp_buffer_append(&ctx->buf, (const uchar *)in, len1);
        cpp_buffer_append(&ctx->buf, (const uchar *)" 1", 2);
    }

    cpp_buffer_append(&ctx->buf, (const uchar *)"\n\0", 2);

    s.flags = 0;
    s.lineno = 1;
    s.pplineno_loc = s.pplineno_val = 0;
    s.fname = s.ppfname = string_ref_ptr(f->name);
    s.p = sp;
    s.file = f;
    s.prev = NULL;
    s.cond = NULL;

    ctx->stream = &s;
    do_define(ctx, &tk);
    ctx->stream = NULL;
}

void cpp_macro_undefine(cpp_context *ctx, const char *in)
{
    cpp_token tk;
    cpp_stream s;
    const uchar *sp;
    cpp_file *f = cpp_file_no(0);

    sp = cpp_buffer_append(&ctx->buf, (const uchar *)in, strlen(in));
    cpp_buffer_append(&ctx->buf, (const uchar *)"\n\0", 2);

    s.flags = 0;
    s.lineno = 1;
    s.pplineno_loc = s.pplineno_val = 0;
    s.fname = s.ppfname = string_ref_ptr(f->name);
    s.p = sp;
    s.file = f;
    s.prev = NULL;
    s.cond = NULL;

    ctx->stream = &s;
    do_undef(ctx, &tk);
    ctx->stream = NULL;
}

/* ------------------------------------------------------------------------ */
/* ---- implementation of the c preprocessor ------------------------------ */
/* ------------------------------------------------------------------------ */

/* Advance next token without run the preprocessor.
 * Can read token from the result of a macro expansion. */
static void cpp_next(cpp_context *ctx, cpp_token *tk)
{
    macro_stack *ms;

    /* Backtrack */
    if (unlikely(ctx->temp.n != 0)) {
        uint i;
        *tk = ctx->temp.tokens[0];
        for (i = 1; i < ctx->temp.n; i++)
            ctx->temp.tokens[i - 1] = ctx->temp.tokens[i];
        ctx->temp.n--;
        return;
    }

    if (ctx->argstream != NULL) {
        ms = ctx->argstream->macro;
        while (ms != NULL) {
            const cpp_token *t = ms->p;
            if (t->kind != TK_eom) {
                *tk = *t;
                ctx->argstream->macro->p++;
                return;
            }
            macro_stack_pop(ctx);
            ms = ctx->argstream->macro;
        }
        *tk = *ctx->argstream->p++;
    } else {
        ms = ctx->file_macro;
        while (ms != NULL) {
            const cpp_token *t = ms->p;
            if (t->kind != TK_eom) {
                *tk = *t;
                ctx->file_macro->p++;
                return;
            }
            macro_stack_pop(ctx);
            ms = ctx->file_macro;
        }
        cpp_lex_scan(ctx->stream, tk);
    }
}

static void cpp_next_nonl(cpp_context *ctx, cpp_token *tk)
{
    do {
        cpp_next(ctx, tk);
    } while (tk->kind == '\n');
}

/* ---- diagnostic -------------------------------------------------------- */

void cpp_error(cpp_context *ctx, cpp_token *tk, const char *s, ...)
{
    va_list ap;
    va_start(ap, s);
    if (ctx->stream != NULL && tk != NULL)
        fprintf(stderr, "\x1b[1;29m%s:%u:\x1b[0m ", ctx->stream->ppfname,
                                                    get_lineno_tok(ctx, tk));
    fprintf(stderr, "\x1b[1;31merror:\x1b[0m ");
    vfprintf(stderr, s, ap);
    fputc('\n', stderr);
    va_end(ap);
    cpp_context_cleanup(ctx);
    exit(1);
}

void cpp_warn(cpp_context *ctx, cpp_token *tk, const char *s, ...)
{
    va_list ap;
    va_start(ap, s);
    if (ctx->stream != NULL && tk != NULL)
        fprintf(stderr, "\x1b[1;29m%s:%u:\x1b[0m ", ctx->stream->ppfname,
                                                    get_lineno_tok(ctx, tk));
    fprintf(stderr, "\x1b[1;35mwarning:\x1b[0m ");
    vfprintf(stderr, s, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static void do_error(cpp_context *ctx, cpp_token *tk)
{
    uint len;
    const uchar *msg;
    uchar buf[1024] = {0};
    cpp_token error_tk = *tk;

    msg = cpp_buffer_append(&ctx->buf, (const uchar *)"#error", 6);
    cpp_next(ctx, tk);

    while (tk->kind != '\n' && tk->kind != TK_eof) {
        if (PREV_SPACE(tk))
            cpp_buffer_append_ch(&ctx->buf, ' ');
        len = cpp_token_splice(tk, buf, sizeof(buf));
        cpp_buffer_append(&ctx->buf, buf, len);
        cpp_next(ctx, tk);
    }

    cpp_buffer_append_ch(&ctx->buf, '\0');
    cpp_error(ctx, &error_tk, "%s", (const char *)msg);
}

/* ---- #line ------------------------------------------------------------- */

static void do_line(cpp_context *ctx, cpp_token *tk)
{
    ulong val;
    cpp_token *tok;
    const uchar *fname;
    uint len, max = sizeof("2147483648");
    uchar buf[PATH_MAX + 1] = {0}, *end = NULL;

    cpp_next(ctx, tk);
    tok = expand_line(ctx, tk, 0);

    if (tok->kind != TK_number) {
        if (tok->kind == '-' && tok[1].kind == TK_number)
            cpp_error(ctx, tok, "line number cannot be negative");
        cpp_error(ctx, tok, "missing line number");
    }

    len = cpp_token_splice(tok, buf, max + 1);
    if (len > max)
        cpp_error(ctx, tok, "line number too large");

    tok++;
    buf[len] = 0;

    val = strtoul((const char *)buf, (char **)&end, 10);
    if (val == 0 && errno == 0)
        cpp_error(ctx, tok, "line number cannot be zero");
    else if (val > INT_MAX)
        cpp_error(ctx, tok, "line number too large");
    else if (end != NULL && *end != '\0')
        cpp_error(ctx, tok, "#line requires a simple digit sequence");

    if (tok->kind == TK_eof)
        goto done;
    else if (tok->kind != TK_string)
        cpp_error(ctx, tok, "filename must be string literal");

    len = cpp_token_splice(tok, buf, PATH_MAX);
    buf[len - 1] = 0;
    fname = buf; fname++; tok++;

    if (tok->kind != TK_eof)
        cpp_error(ctx, tok, "stray token after #line");

done:
    ctx->stream->pplineno_loc = tok->lineno;
    ctx->stream->pplineno_val = val;
    ctx->stream->ppfname = (const char *)cpp_buffer_append(&ctx->buf, fname,
                                                           len);
}

static uint get_lineno_tok(cpp_context *ctx, cpp_token *tk)
{
    uint lineno, lndelta;

    lndelta = tk->lineno - ctx->stream->pplineno_loc;
    lineno = ctx->stream->pplineno_val + lndelta;
    return ctx->stream->pplineno_val ? lineno - 1 : lineno;
}

/* ---- #include stuff ---------------------------------------------------- */

static void cpp_stream_push(cpp_context *ctx, cpp_file *file)
{
    cpp_stream *s = malloc(sizeof(cpp_stream));
    assert(s);
    s->flags = CPP_TOKEN_BOL | CPP_TOKEN_BOF;
    s->pplineno_loc = s->pplineno_val = 0;
    s->lineno = 1;
    s->p = file->data;
    s->fname = s->ppfname = string_ref_ptr(file->name);
    s->file = file;
    s->cond = NULL;
    s->prev = ctx->stream;
    ctx->stream = s;
}

static void cpp_stream_pop(cpp_context *ctx)
{
    if (ctx->stream != NULL) {
        cpp_stream *prev = ctx->stream->prev;
        free(ctx->stream);
        ctx->stream = prev;
    }
}

/* Merge tokens into `buf` until `end_kind` */
static uint join_tokens(cpp_token *tk, cpp_token **end, uchar end_kind,
                        uchar *buf, uint bufsz)
{
    uint len, off = 0;

    while (off < bufsz) {
        if (PREV_SPACE(tk))
            buf[off++] = ' ';
        len = cpp_token_splice(tk, buf + off, MIN(bufsz - off, bufsz));
        off += len; tk++;
        if (tk->kind == TK_eof || tk->kind == end_kind)
            break;
    }

    *end = tk;
    return off;
}

static string_ref search_include_path(const char *name, const char *cwd,
                                      struct stat *sb)
{
    int i;
    char buf[PATH_MAX + 1];
    string_ref pathref = 0;
    const char *search_path;

    if (name[0] != '/') {
        if (cwd != NULL) { /* #include "..." */
            snprintf(buf, sizeof(buf), "%s/%s", cwd, name);
            if (stat(buf, sb) == -1) {
                if (errno != ENOENT)
                    goto done;
                /* else fallthrough and try to #include <...> */
            } else {
                pathref = string_ref_new(buf);
                goto done;
            }
        }
        /* #include <...> */
        for (i = 0; i < g_include_search_path_count; i++) {
            search_path = g_include_search_path[i];
            snprintf(buf, sizeof(buf), "%s/%s", search_path, name);
            if (stat(buf, sb) == -1) {
                if (errno != ENOENT)
                    goto done;
            } else {
                pathref = string_ref_new(buf);
                goto done;
            }
        }
        errno = ENOENT;
    } else {
        if (stat(name, sb) != -1)
            pathref = string_ref_new(buf);
    }

done:
    return pathref;
}

static const char *do_include2(cpp_context *ctx, cpp_token *tk, uchar *buf,
                               const char **cwd, uint *outlen, uchar *is_sys)
{
    uint len = 0;
    const char *path = (const char *)buf;
    cpp_token *tok = expand_line(ctx, tk, 0);

    if (tok->kind == TK_string) {
        len = cpp_token_splice(tok, buf, PATH_MAX);
        buf[len - 1] = 0; path++; len -= 2;
        if (cwd) *cwd = string_ref_ptr(ctx->stream->file->dirpath);
        if (is_sys) *is_sys = 0;
        tok++;
    } else if (tok->kind == '<') {
        tok++;
        len = join_tokens(tok, &tok, '>', buf, PATH_MAX);
        buf[len] = 0;
        if (cwd) *cwd = NULL;
        if (is_sys) *is_sys = 1;
        tok++;
    } else {
        buf[0] = 0;
        path = NULL;
    }

    if (path != NULL && tok->kind != TK_eof) {
        len = 0;
        buf[0] = 0;
        path = NULL;
    }

    if (outlen) *outlen = len;
    return path;
}

static void do_include(cpp_context *ctx, cpp_token *tk)
{
    uint len;
    cpp_macro *m;
    cpp_file *file;
    cpp_token pathtk;
    uchar is_sys = 0;
    struct stat sb = {0};
    uchar buf[PATH_MAX + 1];
    string_ref pathref, nameref;
    const char *cwd = NULL, *name = (const char *)buf;

    cpp_next(ctx, tk);
    pathtk = *tk;

    if (tk->kind == TK_string) {
        len = cpp_token_splice(tk, buf, PATH_MAX);
        buf[len - 1] = 0; name++; len -= 2; /* remove "" */
        cwd = string_ref_ptr(ctx->stream->file->dirpath);
        cpp_next(ctx, tk);
    } else if (tk->kind == '<') {
        cpp_lex_string(ctx->stream, tk, '>');
        len = cpp_token_splice(tk, buf, PATH_MAX);
        buf[len - 1] = 0; len--; /* remove > */
        cpp_next(ctx, tk);
        is_sys = 1;
    } else {
        if (tk->kind != TK_identifier)
            goto include_error;
        name = do_include2(ctx, tk, buf, &cwd, &len, &is_sys);
        if (name == NULL)
            goto include_error;
    }

    if (tk->kind != '\n')
        cpp_error(ctx, &pathtk, "stray token after #include");
    else if (len == 0)
        cpp_error(ctx, &pathtk, "empty filename");

    pathref = search_include_path(name, cwd, &sb);
    if (pathref == 0)
        cpp_error(ctx, &pathtk, "unable to open '%s': %s", name,
                  strerror(errno));

    if (is_sys)
        name = string_ref_ptr(pathref);

    m = hash_table_lookup(&ctx->guarded_file, pathref);
    if (m != NULL && HAS_FLAG(m->flags, CPP_MACRO_GUARD)) {
        file = cpp_file_no(m->fileno);
        if (file != NULL &&
            (uint)sb.st_size == file->size &&
            (uint)sb.st_dev == file->devid &&
            (uint)sb.st_ino == file->inode)
            return;
    }

    file = hash_table_lookup(&ctx->cached_file, pathref);
    if (file == NULL) {
        nameref = string_ref_new(name);
        file = cpp_file_open2(pathref, nameref, &sb);
        if (file == NULL)
            cpp_error(ctx, &pathtk, "unable to open '%s': %s", name,
                      strerror(errno));
    }

    cpp_stream_push(ctx, file);
    return;

include_error:
    cpp_error(ctx, &pathtk, "invalid #include syntax");
}

/* --- #if stuff ---------------------------------------------------------- */

static void cond_stack_push(cpp_context *ctx, cpp_token tk)
{
    cond_stack *cs = malloc(sizeof(cond_stack));
    assert(cs);
    cs->flags = 0;
    cs->guard_name = 0;
    cs->token = tk;
    cs->prev = ctx->stream->cond;
    ctx->stream->cond = cs;
}

static void cond_stack_pop(cpp_context *ctx)
{
    if (ctx->stream->cond != NULL) {
        cond_stack *prev = ctx->stream->cond->prev;
        free(ctx->stream->cond);
        ctx->stream->cond = prev;
    }
}

static void cond_stack_cleanup(cpp_context *ctx)
{
    while (ctx->stream->cond != NULL) {
        cond_stack *prev = ctx->stream->cond->prev;
        free(ctx->stream->cond);
        ctx->stream->cond = prev;
    }
}

static const char *cond_stack_name(cpp_context *ctx)
{
    switch (ctx->stream->cond->ctx) {
    case COND_IF:
        return "#if";
    case COND_IFDEF:
        return "#ifdef";
    case COND_IFNDEF:
        return "#ifndef";
    case COND_ELIF:
        return "#elif";
    case COND_ELSE:
        return "#else";
    default:
        return "conditional directive";
    }
}

/* Skip until #elif/#else/#endif */
static void cond_stack_skip(cpp_context *ctx, cpp_token *tk)
{
    int nested = 0;
    cpp_token hash;
    string_ref dkind;

    while (tk->kind != TK_eof) {
        if (AT_BOL(tk) && tk->kind == '#') {
            hash = *tk;
            cpp_next(ctx, tk);
            if (tk->kind == '\n') {
                cpp_next(ctx, tk);
                continue;
            } else if (tk->kind != TK_identifier) {
                skip_line(ctx, tk);
                continue;
            }
            dkind = tk->p.ref;
            if (nested == 0 && (dkind == g_else || dkind == g_elif ||
                                dkind == g_endif)) {
                cpp_token_array_append(&ctx->temp, &hash);
                cpp_token_array_append(&ctx->temp, tk);
                return;
            } else if (dkind == g_if || dkind == g_ifdef ||
                       dkind == g_ifndef) {
                nested++;
            } else if (dkind == g_endif) {
                nested--;
            }
        }
        cpp_next(ctx, tk);
    }

    if (nested)
        cpp_error(ctx, tk, "unterminated conditional directive");
}

static cond_expr *cond_expr_new(cpp_context *ctx, cpp_token tk)
{
    cond_expr *ce;

    if (unlikely(g_cond_expr_count == CPP_CONDEXPR_MAX))
        cpp_error(ctx, &tk, "too many ast nodes in a #if/#elif expression");

    ce = &g_cond_expr[g_cond_expr_count];
    g_cond_expr_count++;
    return ce;
}

static void cond_expr_clear(void)
{
    if (g_cond_expr_count > 0) {
        memset(g_cond_expr, 0, g_cond_expr_count * sizeof(cond_expr));
        g_cond_expr_count = 0;
    }
}

#define CEXPR_UNARY_PRIO 12

static uchar cond_expr_prio(uchar tk_kind)
{
    switch (tk_kind) {
    case '[':
    case ']':
    case '.':
    case TK_arrow:
    case TK_incr: /* Postfix */
    case TK_decr: /* Postfix */
    case '=':
    case TK_asg_mul:
    case TK_asg_div:
    case TK_asg_mod:
    case TK_asg_add:
    case TK_asg_sub:
    case TK_asg_lshift:
    case TK_asg_rshift:
    case TK_asg_band:
    case TK_asg_bxor:
    case TK_asg_bor:
        /* Invalid */
        return 255;
    case '*':
    case '/':
    case '%':
        return 11;
    case '+':
    case '-':
        return 10;
    case TK_lshift:
    case TK_rshift:
        return 9;
    case '<':
    case '>':
    case TK_le:
    case TK_ge:
        return 8;
    case TK_eq:
    case TK_ne:
        return 7;
    case '&':
        return 6;
    case '^':
        return 5;
    case '|':
        return 4;
    case TK_and:
        return 3;
    case TK_or:
        return 2;
    case '?':
        return 1;
    default:
        return 0;
    }
}

static cond_expr *cond_expr_number(cpp_context *ctx, cpp_token *tok,
                                   cpp_token **end)
{
    ulong val;
    int base = 10;
    const uchar *p;
    cond_expr *ce = NULL;
    cond_expr_value v = {0};
    uchar buf[32], *endp = NULL;
    uint len, max = sizeof("18446744073709551616ULL");

    len = cpp_token_splice(tok, buf, max + 1);
    if (len > max)
        cpp_error(ctx, tok, "integer constant too large");

    buf[len] = 0;
    p = buf;

    if (*p == '0') {
        p++;
        if (*p == 'x' || *p == 'X')
            base = 16;
        else if (isdigit(*p)) /* accept 8 and 9 for diagnostic */
            base = 8;
    }

    val = strtoul((const char *)buf, (char **)&endp, base);
    if (errno != 0 && val == ULONG_MAX)
        cpp_error(ctx, tok, "integer constant too large");

    if (endp != NULL && *endp != '\0') {
        p = endp;
        if (*p == 'u' || *p == 'U') {
            v.is_unsigned = 1;
            p++;
        }
        if (*p == 'l' || *p == 'L') {
            p++;
            if (*p == p[-1])
                p++;
            if (*p == 'u' || *p == 'U') {
                v.is_unsigned = 1;
                p++;
            }
        }
        if (*p != '\0') {
            if (base == 8 && isdigit(*p))
                cpp_error(ctx, tok, "invalid octal constant");
            cpp_error(ctx, tok, "invalid integer constant suffix '%s'", endp);
        }
    }

    if (!v.is_unsigned) {
        if (val > LONG_MAX)
            cpp_warn(ctx, tok, "integer constant '%lu' too large for "
                               "'signed long'", val);
    }

    v.v.u = val;
    ce = cond_expr_new(ctx, *tok);
    ce->kind = CEXPR_VALUE;
    ce->v.val = v;
    *end = tok + 1;
    return ce;
}

static cond_expr *cond_expr_parse(cpp_context *ctx, cpp_token *tok,
                                  cpp_token **end, uchar priority)
{
    uint len;
    uchar prio;
    uchar buf[8];
    cpp_token op;
    cond_expr *ce = NULL;
    cond_expr *cnd, *opr1, *opr2;

    switch (tok->kind) {
    case '(':
        ce = cond_expr_parse(ctx, ++tok, end, 0);
        tok = *end;
        if (tok->kind != ')')
            cpp_error(ctx, tok, "unterminated #if/#elif subexpression");
        else if (ce == NULL)
            cpp_error(ctx, tok, "empty subexpression");
        tok++;
        break;
    case '+':
    case '-':
    case '~':
    case '!':
        op = *tok;
        opr1 = cond_expr_parse(ctx, ++tok, end, CEXPR_UNARY_PRIO);
        tok = *end;
        if (opr1 == NULL)
            cpp_error(ctx, tok, "missing expression in #if/#elif");
        ce = cond_expr_new(ctx, op);
        ce->kind = CEXPR_UNARY;
        ce->v.unary.op = op;
        ce->v.unary.opr = opr1;
        break;
    case TK_identifier:
        tok->kind = TK_number;
        tok->p.ptr = cpp_buffer_append_ch(&ctx->buf, '0');
        tok->length = 1;
        /* fallthrough */
    case TK_number:
        if (HAS_FLAG(tok->flags, CPP_TOKEN_FLNUM))
            cpp_error(ctx, tok, "floating constant cannot be used as a value "
                                "in a #if/#elif expression");
        ce = cond_expr_number(ctx, tok, end);
        tok = *end;
        break;
    case TK_char_const:
        cpp_error(ctx, tok, "character constant is not implemented yet");
        break;
    case TK_string:
        cpp_error(ctx, tok, "string literal cannot be used as a value in a"
                            "#if/#elif expression");
        break;
    case '&':
    case '*':
    case TK_incr: /* Prefix */
    case TK_decr: /* Prefix */
        /* sizeof is not treated as C operator in this case */
        goto invalid_operator;
    default:
        goto done;
    }

    while (tok->kind != TK_eof) {
        prio = cond_expr_prio(tok->kind);
        if (prio == 0 || priority >= prio) {
            break;
        } else if (prio == 255) {
invalid_operator:
            len = cpp_token_splice(tok, buf, sizeof(buf));
            cpp_error(ctx, tok, "operator '%.*s' cannot be used in a #if/#elif"
                                " expression", len, buf);
        }
        if (tok->kind == '?') {
            if (ce == NULL)
                cpp_error(ctx, tok, "missing expression before '?'");
            cnd = ce;
            opr1 = cond_expr_parse(ctx, ++tok, end, 0);
            tok = *end;
            if (opr1 == NULL)
                cpp_error(ctx, tok, "missing expression after '?'");
            else if (tok->kind != ':')
                cpp_error(ctx, tok, "expected ':' in #if/#elif expression "
                                    "to complete '?:' expression");
            opr2 = cond_expr_parse(ctx, ++tok, end, 0);
            tok = *end;
            if (opr2 == NULL)
                cpp_error(ctx, tok, "missing expression after ':'");
            ce = cond_expr_new(ctx, *tok);
            ce->kind = CEXPR_TERNARY;
            ce->v.ternary.cnd = cnd;
            ce->v.ternary.vit = opr1;
            ce->v.ternary.vif = opr2;
        } else {
            op = *tok;
            if (ce == NULL) {
                len = cpp_token_splice(tok, buf, sizeof(buf));
                cpp_error(ctx, tok, "missing value before operator '%.*s'",
                                    len, buf);
            }
            opr1 = ce;
            opr2 = cond_expr_parse(ctx, ++tok, end, prio);
            if (opr2 == NULL) {
                len = cpp_token_splice(tok, buf, sizeof(buf));
                cpp_error(ctx, tok, "missing value after operator '%.*s'",
                                    len, buf);
            }
            tok = *end;
            ce = cond_expr_new(ctx, op);
            ce->kind = CEXPR_BINARY;
            ce->v.binary.op = op;
            ce->v.binary.lhs = opr1;
            ce->v.binary.rhs = opr2;
        }
    }

done:
    *end = tok;
    return ce;
}

/* Note that the function relies on C99 feature which is able to read inactive
 * union member (type punning). */
static cond_expr_value cond_expr_eval2(cpp_context *ctx, cond_expr *expr)
{
    tkchar op;
    cpp_token tk;
    cond_expr_value v = {0}, l, r;

redo_no_recur:
    switch (expr->kind) {
    case CEXPR_VALUE:
        v = expr->v.val;
        break;
    case CEXPR_UNARY:
        tk = expr->v.unary.op;
        op = tk.kind;
        switch (op) {
        case '+':
            expr = expr->v.unary.opr;
            goto redo_no_recur;
        case '-':
            v = cond_expr_eval2(ctx, expr->v.unary.opr);
            if (v.is_unsigned) v.v.u = -v.v.u;
            else v.v.s = -v.v.s;
            break;
        case '!':
            v = cond_expr_eval2(ctx, expr->v.unary.opr);
            if (v.is_unsigned) v.v.u = !v.v.u;
            else v.v.s = !v.v.s;
            break;
        case '~':
            v = cond_expr_eval2(ctx, expr->v.unary.opr);
            if (v.is_unsigned) v.v.u = ~v.v.u;
            else v.v.s = ~v.v.s;
            break;
        }
        break;
    case CEXPR_BINARY:
        tk = expr->v.binary.op;
        op = tk.kind;
        if (op == TK_and || op == TK_or) {
            l = cond_expr_eval2(ctx, expr->v.binary.lhs);
            v.v.u = (!l.is_unsigned && l.v.s) || l.v.u;
            v.is_unsigned = 1;
            if (v.v.u == (op == TK_and)) {
                r = cond_expr_eval2(ctx, expr->v.binary.rhs);
                v.v.u = (!r.is_unsigned && r.v.s) || r.v.u;
            }
            break;
        }
        l = cond_expr_eval2(ctx, expr->v.binary.lhs);
        r = cond_expr_eval2(ctx, expr->v.binary.rhs);
        switch (op) {
        case '*':
            if (l.is_unsigned || r.is_unsigned) {
                v.is_unsigned = 1;
                v.v.u = l.v.u * r.v.u;
            } else {
                v.is_unsigned = 0;
                v.v.s = l.v.s * r.v.s;
            }
            break;
        case '/':
            if ((!r.is_unsigned && l.v.s == 0) || r.v.u == 0)
                cpp_error(ctx, &tk, "division by zero");
            if (l.is_unsigned || r.is_unsigned) {
                v.is_unsigned = 1;
                v.v.u = l.v.u / r.v.u;
            } else {
                v.is_unsigned = 0;
                v.v.s = l.v.s / r.v.s;
            }
            break;
        case '%':
            if ((!r.is_unsigned && l.v.s == 0) || r.v.u == 0)
                cpp_error(ctx, &tk, "division by zero");
            if (l.is_unsigned || r.is_unsigned) {
                v.is_unsigned = 1;
                v.v.u = l.v.u % r.v.u;
            } else {
                v.is_unsigned = 0;
                v.v.s = l.v.s % r.v.s;
            }
            break;
        case '+':
            if (l.is_unsigned || r.is_unsigned) {
                v.is_unsigned = 1;
                v.v.u = l.v.u + r.v.u;
            } else {
                v.is_unsigned = 0;
                v.v.s = l.v.s + r.v.s;
            }
            break;
        case '-':
            if (l.is_unsigned || r.is_unsigned) {
                v.is_unsigned = 1;
                v.v.u = l.v.u - r.v.u;
            } else {
                v.is_unsigned = 0;
                v.v.s = l.v.s - r.v.s;
            }
            break;
        case TK_lshift:
            if (l.is_unsigned || r.is_unsigned) {
                v.is_unsigned = 1;
                v.v.u = l.v.u << r.v.u;
            } else {
                v.is_unsigned = 0;
                v.v.s = l.v.s << r.v.s;
            }
            break;
        case TK_rshift:
            if (l.is_unsigned || r.is_unsigned) {
                v.is_unsigned = 1;
                v.v.u = l.v.u >> r.v.u;
            } else {
                v.is_unsigned = 0;
                v.v.s = l.v.s >> r.v.s;
            }
            break;
        case '<':
            v.is_unsigned = 1;
            if (l.is_unsigned || r.is_unsigned)
                v.v.u = l.v.u < r.v.u;
            else
                v.v.s = l.v.s < r.v.s;
            break;
        case '>':
            v.is_unsigned = 1;
            if (l.is_unsigned || r.is_unsigned)
                v.v.u = l.v.u > r.v.u;
            else
                v.v.s = l.v.s > r.v.s;
            break;
        case TK_le:
            v.is_unsigned = 1;
            if (l.is_unsigned || r.is_unsigned)
                v.v.u = l.v.u <= r.v.u;
            else
                v.v.s = l.v.s <= r.v.s;
            break;
        case TK_ge:
            v.is_unsigned = 1;
            if (l.is_unsigned || r.is_unsigned)
                v.v.u = l.v.u >= r.v.u;
            else
                v.v.s = l.v.s >= r.v.s;
            break;
        case TK_eq:
            v.is_unsigned = 1;
            if (l.is_unsigned || r.is_unsigned)
                v.v.u = l.v.u == r.v.u;
            else
                v.v.s = l.v.s == r.v.s;
            break;
        case TK_ne:
            v.is_unsigned = 1;
            if (l.is_unsigned || r.is_unsigned)
                v.v.u = l.v.u != r.v.u;
            else
                v.v.s = l.v.s != r.v.s;
            break;
        case '&':
            if (l.is_unsigned || r.is_unsigned) {
                v.is_unsigned = 1;
                v.v.u = l.v.u & r.v.u;
            } else {
                v.is_unsigned = 0;
                v.v.s = l.v.s & r.v.s;
            }
            break;
        case '^':
            if (l.is_unsigned || r.is_unsigned) {
                v.is_unsigned = 1;
                v.v.u = l.v.u ^ r.v.u;
            } else {
                v.is_unsigned = 0;
                v.v.s = l.v.s ^ r.v.s;
            }
            break;
        case '|':
            if (l.is_unsigned || r.is_unsigned) {
                v.is_unsigned = 1;
                v.v.u = l.v.u | r.v.u;
            } else {
                v.is_unsigned = 0;
                v.v.s = l.v.s | r.v.s;
            }
            break;
        default:
            break;
        }
        break;
    case CEXPR_TERNARY:
        v = cond_expr_eval2(ctx, expr->v.ternary.cnd);
        if ((!v.is_unsigned && v.v.s) || v.v.u)
            v = cond_expr_eval2(ctx, expr->v.ternary.vit);
        else
            v = cond_expr_eval2(ctx, expr->v.ternary.vif);
        break;
    }

    return v;
}

static uchar cond_expr_eval(cpp_context *ctx, cpp_token *tk)
{
    cond_expr *ce;
    cond_expr_value v;
    cpp_token *end, *tok;

    tok = expand_line(ctx, tk, /* is_expr = */ 1);
    ce = cond_expr_parse(ctx, tok, &end, 0);
    if (ce == NULL)
        cpp_error(ctx, tok, "missing expression in #if/#elif");
    else if (end->kind != TK_eof)
        cpp_error(ctx, end, "stray token after #if/#elif");

    v = cond_expr_eval2(ctx, ce);
    cond_expr_clear();
    return (!v.is_unsigned && v.v.s) || v.v.u;
}

static void do_if(cpp_context *ctx, cpp_token *tk)
{
    uchar included;
    cpp_token iftk = *tk;

    cpp_next(ctx, tk);

    included = cond_expr_eval(ctx, tk);
    cond_stack_push(ctx, iftk);
    ctx->stream->cond->ctx = COND_IF;

    if (!included) {
        ctx->stream->cond->flags |= CPP_COND_SKIP;
        cpp_next(ctx, tk);
        cond_stack_skip(ctx, tk);
    }
}

static void do_ifdef(cpp_context *ctx, cpp_token *tk)
{
    uchar included;
    string_ref name;

    cpp_next(ctx, tk);
    if (tk->kind != TK_identifier)
        cpp_error(ctx, tk, "no macro name given in #ifdef");

    name = tk->p.ref;
    included = hash_table_lookup(&ctx->macro, name) != NULL;
    cond_stack_push(ctx, *tk);

    cpp_next(ctx, tk);
    if (tk->kind != '\n')
        cpp_error(ctx, tk, "stray token after #ifdef");

    ctx->stream->cond->ctx = COND_IFDEF;

    if (!included) {
        ctx->stream->cond->flags |= CPP_COND_SKIP;
        cpp_next(ctx, tk);
        cond_stack_skip(ctx, tk);
    }
}

static void do_ifndef(cpp_context *ctx, cpp_token *tk, cpp_token hash)
{
    cpp_token dir;
    uchar included;
    string_ref dkind;
    string_ref name, guard_name;

    cpp_next(ctx, tk);
    if (tk->kind != TK_identifier)
        cpp_error(ctx, tk, "no macro name given in #ifndef");

    name = tk->p.ref;
    included = hash_table_lookup(&ctx->macro, name) == NULL;
    cond_stack_push(ctx, *tk);

    cpp_next(ctx, tk);
    if (tk->kind != '\n')
        cpp_error(ctx, tk, "stray token after #ifndef");

    ctx->stream->cond->ctx = COND_IFNDEF;

    if (!included) {
        ctx->stream->cond->flags |= CPP_COND_SKIP;
        cpp_next(ctx, tk);
        cond_stack_skip(ctx, tk);
    } else if (HAS_FLAG(hash.flags, CPP_TOKEN_BOF)) {
        cpp_next(ctx, tk);
        if (tk->kind != '#')
            goto putback;
        hash = *tk;
        cpp_next(ctx, tk);
        if (tk->kind == '\n') {
            return;
        } else if (tk->kind != TK_identifier) {
            cpp_token_array_append(&ctx->temp, &hash);
            goto putback;
        }
        dkind = tk->p.ref;
        if (dkind != g_define) {
            cpp_token_array_append(&ctx->temp, &hash);
            goto putback;
        }
        dir = *tk;
        cpp_next(ctx, tk);
        if (tk->kind != TK_identifier) {
            cpp_token_array_append(&ctx->temp, &hash);
            cpp_token_array_append(&ctx->temp, &dir);
            goto putback;
        }
        guard_name = tk->p.ref;
        if (guard_name == name) {
            ctx->stream->cond->flags |= CPP_COND_GUARD;
            ctx->stream->cond->guard_name = guard_name;
        }
        cpp_token_array_append(&ctx->temp, &hash);
        cpp_token_array_append(&ctx->temp, &dir);
    putback:
        cpp_token_array_append(&ctx->temp, tk);
    }
}

static void do_elif(cpp_context *ctx, cpp_token *tk)
{
    uchar included;
    cpp_token eliftk = *tk;

    if (ctx->stream->cond == NULL)
        cpp_error(ctx, tk, "#elif without previous #if");
    else if (ctx->stream->cond->ctx == COND_ELSE)
        cpp_error(ctx, tk, "#elif after #else");

    /* put here for header guard detection */
    ctx->stream->cond->flags |= CPP_COND_ELSIF;

    if (!HAS_FLAG(ctx->stream->cond->flags, CPP_COND_SKIP)) {
        cond_stack_skip(ctx, tk);
        return;
    }

    cpp_next(ctx, tk);

    included = cond_expr_eval(ctx, tk);
    if (!included) {
        cpp_next(ctx, tk);
        cond_stack_skip(ctx, tk);
    } else {
        ctx->stream->cond->token = eliftk;
        ctx->stream->cond->ctx = COND_ELIF;
        ctx->stream->cond->flags &= ~CPP_COND_SKIP;
    }
}

static void do_else(cpp_context *ctx, cpp_token *tk)
{
    cpp_token elsetk = *tk;

    if (ctx->stream->cond == NULL)
        cpp_error(ctx, tk, "#else without previous #if");
    else if (ctx->stream->cond->ctx == COND_ELSE)
        cpp_error(ctx, tk, "#else after #else");

    /* put here for header guard detection */
    ctx->stream->cond->flags |= CPP_COND_ELSIF;

    if (!HAS_FLAG(ctx->stream->cond->flags, CPP_COND_SKIP)) {
        cond_stack_skip(ctx, tk);
        return;
    }

    ctx->stream->cond->token = elsetk;
    ctx->stream->cond->ctx = COND_ELSE;
    ctx->stream->cond->flags &= ~CPP_COND_SKIP;

    cpp_next(ctx, tk);
    if (tk->kind != '\n')
        cpp_error(ctx, tk, "stray token after #else");
}

static void do_endif(cpp_context *ctx, cpp_token *tk)
{
    cpp_macro *m;
    string_ref guard_name, pathref;

    if (ctx->stream->cond == NULL)
        cpp_error(ctx, tk, "#endif without previous #if");

    cpp_next(ctx, tk);
    if (tk->kind != '\n')
        cpp_error(ctx, tk, "stray token after #endif");

    cpp_next(ctx, tk);
    if (tk->kind == TK_eof) {
        guard_name = ctx->stream->cond->guard_name;
        if (ctx->stream->cond->prev == NULL
            && HAS_FLAG(ctx->stream->cond->flags, CPP_COND_GUARD)
            && !HAS_FLAG(ctx->stream->cond->flags, CPP_COND_ELSIF)) {
            m = hash_table_lookup(&ctx->macro, guard_name);
            if (m != NULL) {
                pathref = ctx->stream->file->path;
                m->flags |= CPP_MACRO_GUARD;
                hash_table_insert(&ctx->guarded_file, pathref, m);
            }
        }
    }

    cpp_token_array_append(&ctx->temp, tk);
    cond_stack_pop(ctx);
}

/* ---- macro stuff ------------------------------------------------------- */

#define ADD_BUILTIN(name) do {                              \
        m = macro_new(name, CPP_MACRO_BUILTIN, 0, dummy);   \
        hash_table_insert(&ctx->macro, name, m);            \
    } while (0)

#define ADD_PREDEF(p) cpp_macro_define(ctx, p)

static cpp_token_array dummy;
static cpp_macro *macro_new(string_ref name, uchar flags, ushort fileno,
                            cpp_token_array body);

static void builtin_macro_setup(cpp_context *ctx)
{
    cpp_macro *m;

    ADD_BUILTIN(g__FILE__);
    ADD_BUILTIN(g__LINE__);
    ADD_BUILTIN(g__BASE_FILE__);
    ADD_BUILTIN(g__TIMESTAMP__);
    ADD_BUILTIN(g__DATE__);
    ADD_BUILTIN(g__TIME__);
    ADD_BUILTIN(g_defined);
}

static void predefined_macro_setup(cpp_context *ctx)
{
    ADD_PREDEF("_LP64");
    ADD_PREDEF("__ELF__");
    ADD_PREDEF("__LP64__");
    ADD_PREDEF("__BYTE_ORDER__=__ORDER_LITTLE_ENDIAN__");
    ADD_PREDEF("__ORDER_BIG_ENDIAN__=4321");
    ADD_PREDEF("__ORDER_LITTLE_ENDIAN__=1234");
    ADD_PREDEF("__SIZEOF_DOUBLE__=8");
    ADD_PREDEF("__SIZEOF_FLOAT__=4");
    ADD_PREDEF("__SIZEOF_INT__=4");
    ADD_PREDEF("__SIZEOF_LONG_DOUBLE__=8");
    ADD_PREDEF("__SIZEOF_LONG_LONG__=8");
    ADD_PREDEF("__SIZEOF_LONG__=8");
    ADD_PREDEF("__SIZEOF_POINTER__=8");
    ADD_PREDEF("__SIZEOF_PTRDIFF_T__=8");
    ADD_PREDEF("__SIZEOF_SIZE_T__=8");
    ADD_PREDEF("__SIZEOF_SHORT__=2");
    ADD_PREDEF("__STDC_HOSTED__");
    ADD_PREDEF("__STDC_NO_COMPLEX__");
    ADD_PREDEF("__STDC_VERSION__=201112L");
    ADD_PREDEF("__STDC__");
    ADD_PREDEF("__nkcc__");
    ADD_PREDEF("__amd64");
    ADD_PREDEF("__amd64__");
    ADD_PREDEF("__gnu_linux__");
    ADD_PREDEF("__linux");
    ADD_PREDEF("__linux__");
    ADD_PREDEF("__unix");
    ADD_PREDEF("__unix__");
    ADD_PREDEF("__x86_64");
    ADD_PREDEF("__x86_64__");
    ADD_PREDEF("linux");
    ADD_PREDEF("unix");
}

static void macro_stack_push(cpp_context *ctx, string_ref name)
{
    macro_stack *ms;

    if (g_ms_cache.head != NULL) {
        ms = g_ms_cache.head;
        g_ms_cache.head = ms->prev;
        cpp_token_array_clear(&ms->tok);
    } else {
        ms = malloc(sizeof(macro_stack));
        if (unlikely(ms == NULL))
            cpp_error(ctx, NULL, "macro_stack fails to allocate memory");
        cpp_token_array_setup(&ms->tok, 8);
    }

    if (ctx->argstream != NULL) {
        ms->prev = ctx->argstream->macro;
        ctx->argstream->macro = ms;
    } else {
        ms->prev = ctx->file_macro;
        ctx->file_macro = ms;
    }

    ms->name = name;
}

static void macro_stack_pop(cpp_context *ctx)
{
    arg_stream *arg = ctx->argstream;
    macro_stack *prev, *next_cache = NULL;

    if (arg != NULL && arg->macro != NULL) {
        prev = arg->macro->prev;
        next_cache = ctx->argstream->macro;
        ctx->argstream->macro = prev;
    } else if (ctx->file_macro != NULL) {
        prev = ctx->file_macro->prev;
        next_cache = ctx->file_macro;
        ctx->file_macro = prev;
    }

    if (next_cache != NULL) {
        next_cache->prev = NULL;
        if (g_ms_cache.head == NULL)
            g_ms_cache.head = next_cache;
        else
            g_ms_cache.tail->prev = next_cache;
        g_ms_cache.tail = next_cache;
    }
}

static void macro_stack_cleanup(cpp_context *ctx)
{
    macro_stack *prev;

    (void)ctx;

    while (g_ms_cache.head != NULL) {
        prev = g_ms_cache.head->prev;
        cpp_token_array_cleanup(&g_ms_cache.head->tok);
        free(g_ms_cache.head);
        g_ms_cache.head = prev;
    }
}

static void arg_stream_push(cpp_context *ctx, cpp_macro_arg *arg)
{
    arg_stream *args;

    if (g_as_cache.head != NULL) {
        args = g_as_cache.head;
        g_as_cache.head = args->prev;
    } else {
        args = malloc(sizeof(arg_stream));
        if (unlikely(args == NULL))
            cpp_error(ctx, NULL, "arg_stream fails to allocate memory");
    }

    args->p = arg->body.tokens;
    args->macro = NULL;
    args->prev = ctx->argstream;
    ctx->argstream = args;
}

static void arg_stream_pop(cpp_context *ctx)
{
    macro_stack *ms;
    arg_stream *prev;

    if (ctx->argstream != NULL) {
        prev = ctx->argstream->prev;
        ms = ctx->argstream->macro;
        while (ms != NULL) {
            macro_stack_pop(ctx);
            ms = ctx->argstream->macro;
        }
        ctx->argstream->prev = NULL;
        if (g_as_cache.head == NULL)
            g_as_cache.head = ctx->argstream;
        else
            g_as_cache.tail->prev = ctx->argstream;
        g_as_cache.tail = ctx->argstream;
        ctx->argstream = prev;
    }
}

static void arg_stream_cleanup(cpp_context *ctx)
{
    arg_stream *prev;

    (void)ctx;

    while (g_as_cache.head != NULL) {
        prev = g_as_cache.head->prev;
        free(g_as_cache.head);
        g_as_cache.head = prev;
    }
}

static cpp_macro *macro_new(string_ref name, uchar flags, ushort fileno,
                            cpp_token_array body)
{
    cpp_macro *m = calloc(1, sizeof(cpp_macro));
    assert(m);
    m->name = name;
    m->fileno = fileno;
    m->flags = flags;
    m->body = body;
    return m;
}

static void macro_free(void *p)
{
    cpp_macro *m = (cpp_macro *)p;
    if (m->param != NULL)
        free(m->param);
    if (m->body.tokens != NULL)
        cpp_token_array_cleanup(&m->body);
    free(m);
}

static cpp_macro_arg *macro_arg_new(string_ref param)
{
    cpp_macro_arg *arg = malloc(sizeof(cpp_macro_arg));
    assert(arg);
    arg->flags = param == g__VA_ARGS__ ? CPP_MACRO_VA_ARG : 0;
    arg->param = param;
    cpp_token_array_setup(&arg->body, 4);
    return arg;
}

static void macro_arg_free(void *p)
{
    cpp_macro_arg *arg = (cpp_macro_arg *)p;
    cpp_token_array_cleanup(&arg->body);
    free(arg);
}

static uchar find_param(string_ref *param, uint n_param, cpp_token *tk)
{
    uint i;
    string_ref name;

    if (n_param == 0 || tk->kind != TK_identifier)
        return 0;

    name = tk->p.ref;
    for (i = 0; i < n_param; i++) {
        if (param[i] == name)
            return 1;
    }

    return 0;
}

static void parse_macro_body(cpp_context *ctx, cpp_token *tk,
                             cpp_token_array *body, string_ref *param,
                             uint n_param, uchar flags)
{
    const char *e1 = "'#' is not followed by a macro parameter";
    const char *e2 = "'##' cannot appear at the beginning of replacement list";
    const char *e3 = "'##' cannot appear at the end of replacement list";

    cpp_token_array_setup(body, 8);

    while (tk->kind != '\n' && tk->kind != TK_eof) {
        tk->flags &= ~CPP_TOKEN_BOL;
        if (tk->kind == '#' && HAS_FLAG(flags, CPP_MACRO_FUNC)) {
            cpp_token_array_append(body, tk); /* append # */
            cpp_next(ctx, tk);
            if (!find_param(param, n_param, tk)) {
                free(param);
                cpp_token_array_cleanup(body);
                cpp_error(ctx, tk, "%s", e1);
            }
        } else if (tk->kind == TK_paste) {
            if (body->n == 0) {
                free(param);
                cpp_token_array_cleanup(body);
                cpp_error(ctx, tk, "%s", e2);
            }
            cpp_token_array_append(body, tk); /* append ## */
            cpp_next(ctx, tk);
            if (tk->kind == '\n' || tk->kind == TK_eof) {
                free(param);
                cpp_token_array_cleanup(body);
                cpp_error(ctx, tk, "%s", e3);
            }
        }
        cpp_token_array_append(body, tk);
        cpp_next(ctx, tk);
    }

    tk->kind = TK_eom;
    cpp_token_array_append(body, tk);
}

static uint parse_macro_param(cpp_context *ctx, cpp_token *tk,
                              string_ref **param)
{
    uchar first;
    uint n, cap;
    string_ref *p = NULL;

    first = 1;
    n = 0; cap = 0;
    cpp_next(ctx, tk);

    while (tk->kind != ')') {
        if (!first) {
            if (tk->kind != ',') {
                free(p);
                cpp_error(ctx, tk, "expected ',' or ')'");
            }
            cpp_next(ctx, tk);
        }
        if (n == cap) {
            if (cap != 0) {
                cap *= 2;
                p = realloc(p, cap * sizeof(string_ref));
                assert(p);
            } else {
                cap = 4;
                p = malloc(cap * sizeof(string_ref));
                assert(p);
            }
        }
        if (tk->kind == TK_elipsis) {
            p[n++] = g__VA_ARGS__;
            cpp_next(ctx, tk);
            if (tk->kind != ')') {
                free(p);
                cpp_error(ctx, tk, "expected ')'");
            }
            break;
        }
        if (tk->kind != TK_identifier) {
            free(p);
            cpp_error(ctx, tk, "expected parameter name");
        }
        p[n++] = tk->p.ref;
        cpp_next(ctx, tk);
        first = 0;
    }

    cpp_next(ctx, tk);

    *param = p;
    return n;
}

static void parse_macro_arg(cpp_context *ctx, string_ref param, ht_t *args,
                            cpp_token *tk, string_ref name)
{
    uchar kind;
    uint length;
    int paren = 0;
    cpp_macro_arg *arg = macro_arg_new(param);

    while (1) {
        if (paren == 0 && tk->kind == ')') {
            break;
        } else if (paren == 0 && param != g__VA_ARGS__ && tk->kind == ',') {
            break;
        } else if (tk->kind == TK_eof) {
            hash_table_cleanup_with_free(args, macro_arg_free);
            cpp_error(ctx, tk, "unexpected end of file while parsing macro "
                               "arguments of '%s'", string_ref_ptr(name));
        }
        if (tk->kind == '(')
            paren++;
        else if (tk->kind == ')')
            paren--;
        if (AT_BOL(tk)) {
            tk->flags &= ~CPP_TOKEN_BOL;
            tk->flags |= CPP_TOKEN_SPACE;
        }
        cpp_token_array_append(&arg->body, tk);
        cpp_next_nonl(ctx, tk);
    }

    kind = tk->kind; length = tk->length;
    tk->kind = TK_eof; tk->length = 0;
    cpp_token_array_append(&arg->body, tk);
    tk->kind = kind; tk->length = length;
    hash_table_insert(args, param, arg);
}

static void collect_args(cpp_context *ctx, cpp_macro *m, cpp_token *tk,
                         ht_t *args)
{
    string_ref *param = m->param;
    uint i = 0, n_param = m->n_param;
    uchar first = 1, empty_va_arg = 0;

    hash_table_setup(args, n_param);
    cpp_next_nonl(ctx, tk);

    while (i < n_param) {
        if (!first) {
            if (tk->kind != ',') {
                if (param[i] == g__VA_ARGS__ && tk->kind == ')') {
                    empty_va_arg = 1;
                    break;
                }
                hash_table_cleanup_with_free(args, macro_arg_free);
                cpp_error(ctx, tk, "too few arguments for macro '%s'",
                                    string_ref_ptr(m->name));
            }
            cpp_next_nonl(ctx, tk);
        }
        parse_macro_arg(ctx, param[i++], args, tk, m->name);
        first = 0;
    }

    if (empty_va_arg) {
        cpp_macro_arg *arg = macro_arg_new(g__VA_ARGS__);
        tk->kind = TK_eof; tk->length = 0;
        cpp_token_array_append(&arg->body, tk);
        tk->kind = ')'; tk->length = 1;
        hash_table_insert(args, g__VA_ARGS__, arg);
    } else if (tk->kind != ')') {
        hash_table_cleanup_with_free(args, macro_arg_free);
        cpp_error(ctx, tk, "too many arguments for macro '%s'",
                            string_ref_ptr(m->name));
    }
}

static const char *month[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

static void expand_builtin(cpp_context *ctx, string_ref name,
                           cpp_token *macro_tk, uchar is_expr)
{
    time_t now;
    int len = 0;
    struct tm *tm;
    char buf[4096];
    struct stat sb;
    string_ref defined_op;
    uchar dt = 0, paren = 0, defined_res;

    if (name == g__FILE__) {
        len = snprintf(buf, sizeof(buf), "\"%s\"", ctx->stream->ppfname);
        macro_tk->kind = TK_string;
    } else if (name == g__LINE__) {
        len = snprintf(buf, sizeof(buf), "%u", get_lineno_tok(ctx, macro_tk));
        macro_tk->kind = TK_number;
    } else if (name == g__BASE_FILE__) {
        cpp_stream *s = ctx->stream;
        while (s->prev)
            s = s->prev;
        len = snprintf(buf, sizeof(buf), "\"%s\"", s->fname);
        macro_tk->kind = TK_string;
    } else if (name == g__TIMESTAMP__) {
        if (stat(ctx->stream->fname, &sb) != 0) {
            snprintf(buf, sizeof(buf), "\"??? ??? ?? ??:??:?? ????\"");
        } else {
            buf[0] = '"';
            ctime_r(&sb.st_mtime, (char *)buf + 1);
            buf[25] = '"';
        }
        len = 26;
        macro_tk->kind = TK_string;
    } else if (name == g__DATE__) {
        if (ctx->ppdate == NULL) {
            now = time(NULL);
            tm = localtime(&now);
            len = snprintf(buf, sizeof(buf), "\"%s %2d %d\"",
                                             month[tm->tm_mon],
                                             tm->tm_mday,
                                             tm->tm_year + 1900);
            /* Now cache it */
            ctx->ppdate = cpp_buffer_append(&ctx->buf, (uchar *)buf, len + 1);
        } else {
            len = strlen((const char *)ctx->ppdate);
        }
        dt = 1;
        macro_tk->p.ptr = ctx->ppdate;
        macro_tk->kind = TK_string;
    } else if (name == g__TIME__) {
        if (ctx->pptime == NULL) {
            now = time(NULL);
            tm = localtime(&now);
            len = snprintf(buf, sizeof(buf), "\"%02d:%02d:%02d\"",
                                             tm->tm_hour,
                                             tm->tm_min,
                                             tm->tm_sec);
            /* Now cache it */
            ctx->pptime = cpp_buffer_append(&ctx->buf, (uchar *)buf, len + 1);
        } else {
            len = strlen((const char *)ctx->pptime);
        }
        dt = 1;
        macro_tk->p.ptr = ctx->pptime;
        macro_tk->kind = TK_string;
    } else if (name == g_defined) {
        if (!is_expr)
            return;
        cpp_next(ctx, macro_tk);
        if (macro_tk->kind == '(') {
            paren = 1;
            cpp_next(ctx, macro_tk);
        }
        if (macro_tk->kind != TK_identifier)
            cpp_error(ctx, macro_tk, "operator 'defined' requires an "
                                     "identifier");
        defined_op = macro_tk->p.ref;
        defined_res = (defined_op != g_defined) &&
                      (hash_table_lookup(&ctx->macro, defined_op) != NULL);
        if (paren) {
            cpp_next(ctx, macro_tk);
            if (macro_tk->kind != ')')
                cpp_error(ctx, macro_tk, "missing ')' after 'defined'");
        }
        len = 1;
        buf[0] = defined_res + '0';
        macro_tk->kind = TK_number;
    } else {
        cpp_error(ctx, macro_tk, "unhandled builtin macro '%s'",
                                  string_ref_ptr(name));
    }

    if (!dt)
        macro_tk->p.ptr = cpp_buffer_append(&ctx->buf, (uchar *)buf, len);
    macro_tk->length = len;
    macro_tk->flags &= ~CPP_TOKEN_ESCNL;
}

static cpp_token *expand_line(cpp_context *ctx, cpp_token *tk, uchar is_expr)
{
    uchar kind;
    cpp_token_array_clear(&ctx->line);

    while (1) {
        if (tk->kind == '\n' || tk->kind == TK_eof)
            break;
        else if (tk->kind == TK_identifier && expand(ctx, tk, is_expr))
            ;
        else
            cpp_token_array_append(&ctx->line, tk);
        cpp_next(ctx, tk);
    }

    kind = tk->kind;
    tk->kind = TK_eof;
    cpp_token_array_append(&ctx->line, tk);
    tk->kind = kind;

    return ctx->line.tokens;
}

static void stringize(cpp_context *ctx, cpp_token_array *os, cpp_token *arg_tk,
                      cpp_token_array *_is)
{
    uint i, len;
    cpp_token tmp;
    uchar buf[4096];
    uchar first = 1;
    cpp_stream stream; /* fake stream */
    const cpp_token *is = _is->tokens;
    const uchar *p = cpp_buffer_append_ch(&ctx->buf, '"');

    while (is->kind != TK_eof) {
        if (!first && PREV_SPACE(is))
            cpp_buffer_append_ch(&ctx->buf, ' ');
        len = cpp_token_splice(is, buf, sizeof(buf));
        if (is->kind == TK_string || is->kind == TK_char_const) {
            for (i = 0; i < len; i++) {
                if (buf[i] == '"' || buf[i] == '\\')
                    cpp_buffer_append_ch(&ctx->buf, '\\');
                cpp_buffer_append_ch(&ctx->buf, buf[i]);
            }
        } else {
            cpp_buffer_append(&ctx->buf, buf, len);
        }
        first = 0; is++;
    }

    cpp_buffer_append_ch(&ctx->buf, '"');

    stream.flags = arg_tk->flags & CPP_TOKEN_SPACE;
    stream.lineno = ctx->stream->lineno;
    stream.pplineno_loc = ctx->stream->pplineno_loc;
    stream.pplineno_val = ctx->stream->pplineno_val;
    stream.fname = ctx->stream->fname;
    stream.ppfname = ctx->stream->ppfname;
    stream.file = ctx->stream->file;
    stream.p = p;
    stream.cond = NULL;
    stream.prev = NULL;

    cpp_lex_scan(&stream, &tmp);
    cpp_token_array_append(os, &tmp);
}

static void paste(cpp_context *ctx, cpp_token_array *os, cpp_token *rhs,
                  cpp_token *macro_tk)
{
    int len, len2, n;
    cpp_stream stream; /* fake stream */
    cpp_token tmp, *lhs;
    char buf[1024], buf2[1024], buf3[2049] = {0};

    lhs = &os->tokens[os->n - 1];
    len = (int)cpp_token_splice(lhs, (uchar *)buf, sizeof(buf));
    len2 = (int)cpp_token_splice(rhs, (uchar *)buf2, sizeof(buf2));
    n = snprintf(buf3, 2048, "%.*s%.*s", len, buf, len2, buf2);

    stream.flags = lhs->flags & CPP_TOKEN_SPACE;
    stream.lineno = ctx->stream->lineno;
    stream.pplineno_loc = ctx->stream->pplineno_loc;
    stream.pplineno_val = ctx->stream->pplineno_val;
    stream.fname = ctx->stream->fname;
    stream.ppfname = ctx->stream->ppfname;
    stream.p = cpp_buffer_append(&ctx->buf, (const uchar *)buf3, n + 1);
    stream.file = ctx->stream->file;
    stream.cond = NULL;
    stream.prev = NULL;

    cpp_lex_scan(&stream, &tmp);
    if (tmp.kind == TK_eof)
        cpp_error(ctx, macro_tk, "## produced invalid pp-token '%s'", buf3);

    *lhs = tmp;

    cpp_lex_scan(&stream, &tmp);
    if (tmp.kind != TK_eof)
        cpp_error(ctx, macro_tk, "## produced invalid pp-token '%s'", buf3);
}

static uchar is_active_macro(cpp_context *ctx, string_ref name)
{
    macro_stack *ms = ctx->argstream ? ctx->argstream->macro : ctx->file_macro;

    while (ms != NULL) {
        if (ms->name == name)
            return 1;
        ms = ms->prev;
    }
    return 0;
}

static void expand_arg(cpp_context *ctx, cpp_macro_arg *arg,
                       cpp_token_array *os, cpp_token *param_tk)
{
    cpp_token tk;
    uint i = os->n;

    arg_stream_push(ctx, arg);

    while (1) {
        cpp_next(ctx, &tk);
        if (tk.kind == TK_eof) {
            break;
        } else if (tk.kind == TK_identifier && expand(ctx, &tk, 0)) {
            ;
        } else {
            tk.flags &= ~CPP_TOKEN_BOL;
            cpp_token_array_append(os, &tk);
        }
    }

    if (i < os->n) {
        os->tokens[i].flags |= param_tk->flags;
        if (!PREV_SPACE(param_tk))
            os->tokens[i].flags &= ~CPP_TOKEN_SPACE;
    }

    arg_stream_pop(ctx);
}

static cpp_macro_arg *find_arg(ht_t *args, cpp_token *tk)
{
    string_ref name;

    if (tk->kind != TK_identifier || args == NULL || args->count == 0)
        return NULL;

    name = tk->p.ref;
    return hash_table_lookup(args, name);
}

static void subst(cpp_context *ctx, cpp_macro *m, cpp_token *macro_tk,
                  ht_t *args, cpp_token_array *os)
{
    cpp_token *is = m->body.tokens;

    while (is->kind != TK_eom) {
        if (is->kind == '#' && args != NULL) {
            stringize(ctx, os, is, &find_arg(args, is + 1)->body);
            is += 2;
            continue;
        }

        /* ## rhs */
        if (is->kind == TK_paste) {
            cpp_macro_arg *arg = find_arg(args, ++is);
            if (arg != NULL) {
                cpp_token *is2 = arg->body.tokens;
                if (is2->kind == TK_eof)
                    ;
                else if (os->n == 0)
                    cpp_token_array_append(os, is2++);
                else
                    paste(ctx, os, is2++, macro_tk);
                while (is2->kind != TK_eof)
                    cpp_token_array_append(os, is2++);
            } else {
                paste(ctx, os, is, macro_tk);
            }
            is++;
            continue;
        }

        cpp_macro_arg *arg = find_arg(args, is);

        if (arg != NULL) { /* We found a parameter and its arguments */
            if (is[1].kind == TK_paste) {
                /* Need to suppress macro expansion */
                cpp_token *rhs = is + 2;
                cpp_token *is2 = arg->body.tokens;
                if (is2->kind == TK_eof) {
                    /* lhs is empty, we don't need to paste it */
                    cpp_macro_arg *arg2 = find_arg(args, rhs);
                    if (arg2 != NULL) {
                        is2 = arg2->body.tokens;
                        while (is2->kind != TK_eof)
                            cpp_token_array_append(os, is2++);
                    } else {
                        cpp_token_array_append(os, rhs);
                    }
                    is += 3;
                } else {
                    /* The first token from the argument must be either have
                     * CPP_TOKEN_SPACE if the parameter token has it or not
                     * at all. In the later case, the CPP_TOKEN_SPACE must
                     * be removed. */
                    is2->flags &= ~CPP_TOKEN_SPACE;
                    is2->flags |= (is->flags & CPP_TOKEN_SPACE);
                    while (is2->kind != TK_eof)
                        cpp_token_array_append(os, is2++);
                    is++; /* Handle ## in the next iteration */
                }
            } else {
                /* Handle argument */
                expand_arg(ctx, arg, os, is++);
            }
            continue;
        }

        /* Remaining token from the replacement list. */
        is->lineno = macro_tk->lineno;
        cpp_token_array_append(os, is++);
    }

    cpp_token_array_append(os, is); /* TK_eom */
}

static uchar expand(cpp_context *ctx, cpp_token *tk, uchar is_expr)
{
    string_ref name = tk->p.ref;
    cpp_macro *m = hash_table_lookup(&ctx->macro, name);
    if (m == NULL)
        return 0;

    if (HAS_FLAG(m->flags, CPP_MACRO_BUILTIN)) {
        expand_builtin(ctx, name, tk, is_expr);
        return 0; /* Special; No rescanning needed */
    }

    macro_stack *ms = NULL;
    cpp_token macro_tk = *tk;

    if (HAS_FLAG(tk->flags, CPP_TOKEN_NOEXPAND))
        return 0;

    if (is_active_macro(ctx, name)) {
        tk->flags |= CPP_TOKEN_NOEXPAND;
        return 0;
    }

    if (HAS_FLAG(m->flags, CPP_MACRO_FUNC)) {
        ht_t args;
        cpp_next_nonl(ctx, tk);
        if (tk->kind != '(') {
            cpp_token_array_append(&ctx->temp, tk);
            *tk = macro_tk;
            return 0;
        }
        collect_args(ctx, m, tk, &args);
        macro_stack_push(ctx, name);
        ms = ctx->argstream ? ctx->argstream->macro : ctx->file_macro;
        subst(ctx, m, &macro_tk, &args, &ms->tok);
        hash_table_cleanup_with_free(&args, macro_arg_free);
    } else {
        macro_stack_push(ctx, name);
        ms = ctx->argstream ? ctx->argstream->macro : ctx->file_macro;
        subst(ctx, m, &macro_tk, NULL, &ms->tok);
    }

    ms->p = ms->tok.tokens;
    ms->tok.tokens[0].flags |= macro_tk.flags;
    ms->tok.tokens[0].lineno = macro_tk.lineno;
    return 1;
}

static uchar macro_equal(cpp_macro *old_m, cpp_macro *new_m)
{
    uint i;
    cpp_token *tk1, *tk2;
    uchar type1 = HAS_FLAG(old_m->flags, CPP_MACRO_FUNC);
    uchar type2 = HAS_FLAG(new_m->flags, CPP_MACRO_FUNC);

    if (type1 != type2)
        return 0;

    if (old_m->body.n != new_m->body.n)
        return 0;

    if (type1 == CPP_MACRO_FUNC) {
        if (old_m->n_param != new_m->n_param)
            return 0;
        for (i = 0; i < old_m->n_param; i++) {
            if (old_m->param[i] != new_m->param[i])
                return 0;
        }
    }

    for (i = 0; i < old_m->body.n; i++) {
        tk1 = &old_m->body.tokens[i];
        tk2 = &new_m->body.tokens[i];
        if (tk1->kind != TK_eom && !cpp_token_equal(tk1, tk2))
            return 0;
    }

    return 1;
}

static void do_define(cpp_context *ctx, cpp_token *tk)
{
    cpp_file *file;
    uchar flags = 0;
    uint n_param = 0;
    cpp_token_array body;
    cpp_macro *m, *old_m, tmp;
    string_ref name, *param = NULL;

    cpp_next(ctx, tk);
    if (tk->kind != TK_identifier)
        cpp_error(ctx, tk, "no macro name given in #define");

    name = tk->p.ref;
    if (name >= g__VA_ARGS__ && name <= g_defined) {
        if (name == g_defined)
            cpp_error(ctx, tk, "'defined' cannot be used as a macro name");
        else if (name == g__VA_ARGS__)
            cpp_warn(ctx, tk, "__VA_ARGS__ used as a macro name has no effect");
    }

    old_m = hash_table_lookup(&ctx->macro, name);
    cpp_next(ctx, tk);

    if (tk->kind == '(' && !PREV_SPACE(tk)) {
        n_param = parse_macro_param(ctx, tk, &param);
        flags = CPP_MACRO_FUNC;
    }

    parse_macro_body(ctx, tk, &body, param, n_param, flags);
    body.tokens[0].flags &= ~CPP_TOKEN_SPACE;

    if (unlikely(old_m != NULL)) {
        /* Slow... */
        tmp.flags = flags;
        tmp.body = body;
        tmp.param = param;
        tmp.n_param = n_param;
        if (macro_equal(old_m, &tmp)) {
            cpp_token_array_cleanup(&body);
            free(param);
            return;
        }
        if (HAS_FLAG(old_m->flags, CPP_MACRO_GUARD)) {
            cpp_warn(ctx, tk, "'%s' already defined as header guard macro",
                              string_ref_ptr(name));
            file = cpp_file_no(old_m->fileno);
            hash_table_remove(&ctx->guarded_file, file->path);
        } else {
            cpp_warn(ctx, tk, "'%s' redefined", string_ref_ptr(name));
        }
        if (HAS_FLAG(flags, CPP_MACRO_FUNC)) {
            if (HAS_FLAG(old_m->flags, CPP_MACRO_FUNC))
                free(old_m->param);
            old_m->param = param;
            old_m->n_param = n_param;
        }
        old_m->flags = flags;
        old_m->fileno = ctx->stream->file->no;
        cpp_token_array_cleanup(&old_m->body);
        old_m->body = body;
    } else {
        m = macro_new(name, flags, ctx->stream->file->no, body);
        if (HAS_FLAG(flags, CPP_MACRO_FUNC)) {
            m->param = param;
            m->n_param = n_param;
        }
        hash_table_insert(&ctx->macro, name, m);
    }
}

static void do_undef(cpp_context *ctx, cpp_token *tk)
{
    cpp_macro *m;
    string_ref name;

    cpp_next(ctx, tk);
    if (tk->kind != TK_identifier)
        cpp_error(ctx, tk, "no macro name given in #undef");

    name = tk->p.ref;
    if (name >= g__VA_ARGS__ && name <= g_defined) {
        if (name == g_defined)
            cpp_error(ctx, tk, "'defined' cannot be used as a macro name");
        else if (name == g__VA_ARGS__)
            cpp_warn(ctx, tk, "__VA_ARGS__ used as a macro name has no effect");
        else
            cpp_warn(ctx, tk, "undefining builtin macro '%s'",
                              string_ref_ptr(name));
    }

    m = hash_table_remove(&ctx->macro, name);
    if (m != NULL) {
        if (HAS_FLAG(m->flags, CPP_MACRO_GUARD))
            cpp_warn(ctx, tk, "undefining header guard macro '%s'",
                     string_ref_ptr(name));
        macro_free((void *)m);
    }

    cpp_next(ctx, tk);
    if (tk->kind != '\n')
        cpp_error(ctx, tk, "stray token after #undef");
}

/* ------------------------------------------------------------------------ */

static void skip_line(cpp_context *ctx, cpp_token *tk)
{
    do {
        cpp_next(ctx, tk);
    } while (tk->kind != '\n' && tk->kind != TK_eof);
}

static uchar is_hash(cpp_context *ctx, cpp_token *tk)
{
    return AT_BOL(tk) && tk->kind == '#' && ctx->file_macro == NULL;
}

/* Advance next token and run the preprocessor and do macro expansion if
 * necessary. */
static void cpp_preprocess(cpp_context *ctx, cpp_token *tk)
{
    cpp_token hash;
    cpp_file *file;
    string_ref pathref, dkind;

    while (1) {
        cpp_next(ctx, tk);
        if (tk->kind == TK_eof) {
            if (ctx->stream->cond != NULL) {
                hash = ctx->stream->cond->token;
                cpp_error(ctx, &hash, "unterminated %s", cond_stack_name(ctx));
            }
            file = ctx->stream->file;
            pathref = file->path;
            if (!hash_table_lookup(&ctx->guarded_file, pathref)) {
                /* Cache the file, no more cpp_file_open2() if the file is
                 * #included multiple times. */
                hash_table_insert(&ctx->cached_file, pathref, file);
            }
            cpp_stream_pop(ctx);
            if (ctx->stream == NULL)
                return; /* No more input left. */
            continue;
        } else if (tk->kind == '\n') {
            continue;
        }

        if (tk->kind == TK_identifier && expand(ctx, tk, 0))
            continue;
        else if (!is_hash(ctx, tk))
            return;

        hash = *tk;
        cpp_next(ctx, tk);

        if (tk->kind == '\n') /* null directive */
            continue;
        else if (tk->kind != TK_identifier)
            cpp_error(ctx, tk, "preprocessing directive requires an identifier");

        dkind = tk->p.ref;

        /* #if */
        if (dkind == g_if) {
            do_if(ctx, tk);
            continue;
        }

        /* #ifdef */
        if (dkind == g_ifdef) {
            do_ifdef(ctx, tk);
            continue;
        }

        /* #ifndef, hash is used to detect header guard */
        if (dkind == g_ifndef) {
            do_ifndef(ctx, tk, hash);
            continue;
        }

        /* #elif */
        if (dkind == g_elif) {
            do_elif(ctx, tk);
            continue;
        }

        /* #else */
        if (dkind == g_else) {
            do_else(ctx, tk);
            continue;
        }

        /* #endif */
        if (dkind == g_endif) {
            do_endif(ctx, tk);
            continue;
        }

        /* #define */
        if (dkind == g_define) {
            do_define(ctx, tk);
            continue;
        }

        /* #undef */
        if (dkind == g_undef) {
            do_undef(ctx, tk);
            continue;
        }

        /* #include */
        if (dkind == g_include) {
            do_include(ctx, tk);
            continue;
        }

        /* #line */
        if (dkind == g_line) {
            do_line(ctx, tk);
            continue;
        }

        /* #error */
        if (dkind == g_error) {
            do_error(ctx, tk);
            continue;
        }

        /* #pragma, not yet implemented */
        if (dkind == g_pragma) {
            skip_line(ctx, tk);
            continue;
        }

        cpp_error(ctx, tk, "unknown directive '%s'", string_ref_ptr(dkind));
    }
}
