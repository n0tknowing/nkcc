/* Current issues:
 * - Token spacing
 */
#include "cpp.h"

static void cpp_preprocess(cpp_context *ctx, cpp_token *tk);
static void cpp_stream_push(cpp_context *ctx, cpp_file *file);
static void cpp_stream_pop(cpp_context *ctx);
static void builtin_macro_setup(cpp_context *ctx);
static void macro_stack_pop(cpp_context *ctx);
static void cpp_buffer_cleanup(cpp_context *ctx);
static void cond_stack_cleanup(cpp_context *ctx);
static void macro_free(void *p);
static cpp_token *expand_line(cpp_context *ctx, cpp_token *tk);
static uchar expand(cpp_context *ctx, cpp_token *tk);


/* ------------------------------------------------------------------------ */

static string_ref g__VA_ARGS__,
                  g__FILE__,
                  g__LINE__,
                  g__DATE__,
                  g__TIME__,
                  g__COUNTER__,
                  g__BASE_FILE__,
                  g__TIMESTAMP__,
                  g_defined;


/* ------------------------------------------------------------------------ */

void cpp_context_setup(cpp_context *ctx)
{
    cpp_file_setup();
    string_pool_setup();

    g__VA_ARGS__ = LITREF("__VA_ARGS__");
    g__FILE__ = LITREF("__FILE__");
    g__LINE__ = LITREF("__LINE__");
    g__COUNTER__ = LITREF("__COUNTER__");
    g__BASE_FILE__ = LITREF("__BASE_FILE__");
    g__TIMESTAMP__ = LITREF("__TIMESTAMP__");
    g__DATE__ = LITREF("__DATE__");
    g__TIME__ = LITREF("__TIME__");
    g_defined = LITREF("defined");

    memset(ctx, 0, sizeof(cpp_context));

    hash_table_setup(&ctx->cached_file, 16);
    hash_table_setup(&ctx->macro, 1024);

    cpp_token_array_setup(&ctx->temp, 4);
    cpp_token_array_setup(&ctx->line, 8);

    cpp_lex_setup(ctx);

    builtin_macro_setup(ctx);
}

void cpp_context_cleanup(cpp_context *ctx)
{
    cpp_file_cleanup();
    string_pool_cleanup();

    while (ctx->stream != NULL) {
        cond_stack_cleanup(ctx);
        cpp_stream_pop(ctx);
    }

    while (ctx->file_macro != NULL)
        macro_stack_pop(ctx);

    cpp_buffer_cleanup(ctx);

    cpp_token_array_cleanup(&ctx->line);
    cpp_token_array_cleanup(&ctx->temp);
    cpp_token_array_cleanup(&ctx->ts);

    hash_table_cleanup(&ctx->cached_file);
    hash_table_cleanup_with_free(&ctx->macro, macro_free);

    memset(ctx, 0, sizeof(cpp_context));
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
    int delta, i;
    uint lineno = 1;
    cpp_stream_push(ctx, file);

    while (1) {
        cpp_preprocess(ctx, &tk);
        if (tk.kind == TK_eof)
            break;
        if (HAS_FLAG(tk.flags, CPP_TOKEN_BOL)) {
            delta = (int)tk.lineno - (int)lineno;
            if (delta > 0 && delta < 6) {
                for (i = 0; i < delta; i++)
                    fputc('\n', fp);
            } else {
                fputc('\n', fp);
            }
            lineno = tk.lineno;
        }
        cpp_token_print(fp, &tk);
    }

    if (tk.lineno > 1)
        fputc('\n', fp);
}

/* ---- cpp_buffer -------------------------------------------------------- */

static cpp_buffer *cpp_buffer_new(uint cap)
{
    cpp_buffer *buf = malloc(sizeof(cpp_buffer));
    assert(buf);
    buf->prev = NULL;
    buf->len = 0;
    buf->cap = cap;
    buf->data = malloc(cap);
    assert(buf->data);
    return buf;
}

static void cpp_buffer_cleanup(cpp_context *ctx)
{
    while (ctx->buf != NULL) {
        cpp_buffer *prev = ctx->buf->prev;
        free(ctx->buf->data);
        free(ctx->buf);
        ctx->buf = prev;
    }
}

const uchar *cpp_buffer_append_ch(cpp_context *ctx, uchar ch)
{
    const uchar *r;
    cpp_buffer *buf;

    if (ctx->buf == NULL) {
        ctx->buf = cpp_buffer_new(4096);
    } else if (ctx->buf->len >= ctx->buf->cap) {
        buf = cpp_buffer_new(4096);
        buf->prev = ctx->buf;
        ctx->buf = buf;
    }

    buf = ctx->buf;
    r = buf->data + buf->len;
    buf->data[buf->len++] = ch;
    return r;
}

const uchar *cpp_buffer_append(cpp_context *ctx, const uchar *p, uint psize)
{
    const uchar *r;
    cpp_buffer *buf;

    if (unlikely(psize == 1)) {
        return cpp_buffer_append_ch(ctx, *p);
    } else if (ctx->buf == NULL) {
        ctx->buf = cpp_buffer_new(4096);
    } else if (ctx->buf->len + psize >= ctx->buf->cap) {
        buf = cpp_buffer_new(4096);
        buf->prev = ctx->buf;
        ctx->buf = buf;
    }

    buf = ctx->buf;
    r = buf->data + buf->len;
    memcpy(buf->data + buf->len, p, psize);
    buf->len += psize;
    return r;
}

/* ------------------------------------------------------------------------ */
/* ---- implementation of the c preprocessor ------------------------------ */
/* ------------------------------------------------------------------------ */

/* Advance next token without run the preprocessor.
 * Can read token from the result of a macro expansion. */
static void cpp_next(cpp_context *ctx, cpp_token *tk)
{
    /* Backtrack, happen when try to call func-like macro without '(' */
    if (unlikely(ctx->temp.n != 0)) {
        uint i;
        *tk = *ctx->temp.tokens;
        for (i = 1; i < ctx->temp.n; i++)
            ctx->temp.tokens[i - 1] = ctx->temp.tokens[i];
        ctx->temp.n--;
        return;
    }

    if (ctx->argstream != NULL) {
        macro_stack *macro = ctx->argstream->macro;
        while (macro != NULL) {
            const cpp_token *t = macro->p;
            if (t->kind != TK_eom) {
                *tk = *t;
                ctx->argstream->macro->p++;
                return;
            }
            macro_stack_pop(ctx);
            macro = ctx->argstream->macro;
        }
        *tk = *ctx->argstream->p++;
    } else {
        macro_stack *macro = ctx->file_macro;
        while (ctx->file_macro != NULL) {
            const cpp_token *t = macro->p;
            if (t->kind != TK_eom) {
                *tk = *t;
                ctx->file_macro->p++;
                return;
            }
            macro_stack_pop(ctx);
            macro = ctx->file_macro;
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

/* Merge tokens into `buf` until `end_kind` */
static uint join_tokens(cpp_token *tk, cpp_token **end, uchar end_kind,
                        uchar *buf, uint bufsz)
{
    ushort i;
    uint len, off = 0;

    while (off < bufsz) {
        for (i = 0; i < tk->wscount; i++)
            buf[off++] = ' ';
        len = cpp_token_splice(tk, buf + off, MIN(bufsz - off, bufsz));
        off += len; tk++;
        if (tk->kind == TK_eof || tk->kind == end_kind)
            break;
    }

    *end = tk;
    return off;
}

/* ---- diagnostic -------------------------------------------------------- */

static void cpp_error(cpp_context *ctx, cpp_token *tk, const char *s, ...)
{
    va_list ap;
    va_start(ap, s);
    fprintf(stderr, "\x1b[1;29m%s:%u:\x1b[0m ", ctx->stream->fname, tk->lineno);
    fprintf(stderr, "\x1b[1;31merror:\x1b[0m ");
    vfprintf(stderr, s, ap);
    fputc('\n', stderr);
    va_end(ap);
    cpp_context_cleanup(ctx);
    exit(1);
}

static void cpp_warn(cpp_context *ctx, cpp_token *tk, const char *s, ...)
{
    va_list ap;
    va_start(ap, s);
    fprintf(stderr, "\x1b[1;29m%s:%u:\x1b[0m ", ctx->stream->fname, tk->lineno);
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

    msg = cpp_buffer_append(ctx, (const uchar *)"#error", 6);
    cpp_next(ctx, tk);

    while (tk->kind != '\n' && tk->kind != TK_eof) {
        if (tk->wscount > 0)
            cpp_buffer_append_ch(ctx, ' ');
        len = cpp_token_splice(tk, buf, sizeof(buf));
        cpp_buffer_append(ctx, buf, len);
        cpp_next(ctx, tk);
    }

    cpp_buffer_append_ch(ctx, '\0');
    cpp_error(ctx, &error_tk, "%s", (const char *)msg);
}

/* ---- #line ------------------------------------------------------------- */

static void do_line(cpp_context *ctx, cpp_token *tk)
{
    const uchar *fname;
    unsigned long val;
    cpp_token line_tk, *tok;
    uint len, max = sizeof("2147483648");
    uchar buf[PATH_MAX + 1] = {0}, *end = NULL;

    line_tk = *tk;
    cpp_next(ctx, tk);
    tok = expand_line(ctx, tk);

    if (tok->kind != TK_number) {
        if (tok->kind == '-' && tok[1].kind == TK_number)
            cpp_error(ctx, &line_tk, "line number cannot be negative");
        cpp_error(ctx, &line_tk, "missing line number");
    }

    len = cpp_token_splice(tok, buf, max + 1);
    if (len > max)
        cpp_error(ctx, &line_tk, "line number too large");

    tok++;
    buf[len] = 0;

    val = strtoul((const char *)buf, (char **)&end, 10);
    if (val == 0 && errno == 0)
        cpp_error(ctx, &line_tk, "line number cannot be zero");
    else if (val > INT_MAX)
        cpp_error(ctx, &line_tk, "line number too large");

    ctx->stream->pplineno = val;

    if (tok->kind == TK_eof)
        return;
    else if (tok->kind != TK_string)
        cpp_error(ctx, &line_tk, "filename must be string literal");

    len = cpp_token_splice(tok, buf, PATH_MAX);
    buf[len-1] = 0;
    fname = buf; fname++; tok++;
    ctx->stream->ppfname = (const char *)cpp_buffer_append(ctx, fname, len);

    if (tok->kind != TK_eof)
        cpp_error(ctx, &line_tk, "stray token after #line");
}

/* ---- #include stuff ---------------------------------------------------- */

static void cpp_stream_push(cpp_context *ctx, cpp_file *file)
{
    cpp_stream *s = malloc(sizeof(cpp_stream));
    assert(s);
    s->flags = CPP_TOKEN_BOL | CPP_TOKEN_BOF;
    s->pplineno = 0;
    s->lineno = 1;
    s->p = file->data;
    s->fname = s->ppfname = file->name;
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

static uchar *do_include2(cpp_context *ctx, cpp_token *tk, uchar *buf,
                          const char **cwd, uint *outlen)
{
    uint len = 0;
    uchar *path = buf;
    cpp_token *tok = expand_line(ctx, tk);

    if (tok->kind == TK_string) {
        len = cpp_token_splice(tok, buf, PATH_MAX);
        buf[len-1] = 0; path++; len -= 2;
        if (cwd) *cwd = ctx->stream->file->dirpath;
        tok++;
    } else if (tok->kind == '<') {
        tok++;
        len = join_tokens(tok, &tok, '>', buf, PATH_MAX);
        buf[len] = 0;
        if (cwd) *cwd = NULL;
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
    cpp_file *file;
    cpp_token pathtk;
    string_ref pathref;
    const char *cwd = NULL;
    uchar in_macro_expansion = 0;
    uchar buf[PATH_MAX + 1], *path = buf;

    cpp_next(ctx, tk);
    pathtk = *tk;

    if (tk->kind == TK_string) {
        len = cpp_token_splice(tk, buf, PATH_MAX);
        buf[len-1] = 0; path++; len -= 2;
        cwd = ctx->stream->file->dirpath;
        cpp_next(ctx, tk);
    } else if (tk->kind == '<') {
        cpp_lex_string(ctx->stream, tk, '>');
        len = cpp_token_splice(tk, buf, PATH_MAX);
        buf[len-1] = 0; len--;
        cpp_next(ctx, tk);
    } else {
        if (tk->kind != TK_identifier)
            goto include_error;
        path = do_include2(ctx, tk, buf, &cwd, &len);
        if (path == NULL)
            goto include_error;
    }

    if (tk->kind != '\n')
        cpp_error(ctx, &pathtk, "stray token after #include");
    else if (len == 0)
        cpp_error(ctx, &pathtk, "empty filename");

    pathref = string_ref_newlen((const char *)path, len);
    file = hash_table_lookup(&ctx->cached_file, pathref);
    if (file == NULL) {
        file = cpp_file_open((const char *)path, cwd);
        if (file == NULL)
            cpp_error(ctx, &pathtk, "unable to open '%s': %s", path, strerror(errno));
    }

    cpp_stream_push(ctx, file);
    return;

include_error:
    cpp_error(ctx, &pathtk, "invalid #include syntax");
}

/* --- #if stuff ---------------------------------------------------------- */

static void cond_stack_push(cpp_context *ctx, cpp_token *tk, uchar included)
{
    cond_stack *cs = malloc(sizeof(cond_stack));
    assert(cs);
    cs->flags = 0;
    cs->token = *tk;
    cs->prev = ctx->stream->cond;
    cs->ctx = included ? COND_IF : COND_DEAD;
    ctx->stream->cond = cs;
}

static void cond_stack_cleanup(cpp_context *ctx)
{
    while (ctx->stream->cond != NULL) {
        cond_stack *prev = ctx->stream->cond->prev;
        free(ctx->stream->cond);
        ctx->stream->cond = prev;
    }
}

/* ---- macro stuff ------------------------------------------------------- */

#define ADD_BUILTIN(name) do {                          \
        m = macro_new(name, CPP_MACRO_BUILTIN, dummy);  \
        hash_table_insert(&ctx->macro, name, m);        \
    } while (0)

static cpp_macro *macro_new(string_ref name, uchar flags, cpp_token_array body);
static cpp_token_array dummy;

static void builtin_macro_setup(cpp_context *ctx)
{
    cpp_macro *m;

    ADD_BUILTIN(g__FILE__);
    ADD_BUILTIN(g__LINE__);
    ADD_BUILTIN(g__COUNTER__);
    ADD_BUILTIN(g__BASE_FILE__);
    ADD_BUILTIN(g__TIMESTAMP__);
    ADD_BUILTIN(g__DATE__);
    ADD_BUILTIN(g__TIME__);
}

static void macro_stack_push(cpp_context *ctx, string_ref name)
{
    macro_stack *ms = malloc(sizeof(macro_stack));
    assert(ms);
    ms->name = name;
    cpp_token_array_setup(&ms->tok, 8);

    if (ctx->argstream != NULL) {
        ms->prev = ctx->argstream->macro;
        ctx->argstream->macro = ms;
    } else {
        ms->prev = ctx->file_macro;
        ctx->file_macro = ms;
    }
}

static void macro_stack_pop(cpp_context *ctx)
{
    macro_stack *prev;
    arg_stream *arg = ctx->argstream;

    if (arg != NULL && arg->macro != NULL) {
        prev = arg->macro->prev;
        cpp_token_array_cleanup(&arg->macro->tok);
        free(arg->macro);
        ctx->argstream->macro = prev;
    } else if (ctx->file_macro != NULL) {
        prev = ctx->file_macro->prev;
        cpp_token_array_cleanup(&ctx->file_macro->tok);
        free(ctx->file_macro);
        ctx->file_macro = prev;
    }
}

static void arg_stream_push(cpp_context *ctx, cpp_macro_arg *arg)
{
    arg_stream *args = malloc(sizeof(arg_stream));
    assert(args);
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
        free(ctx->argstream);
        ctx->argstream = prev;
    }
}

static cpp_macro *macro_new(string_ref name, uchar flags, cpp_token_array body)
{
    cpp_macro *m = calloc(1, sizeof(cpp_macro));
    assert(m);
    m->name = name;
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
    uint i, len;
    string_ref name;
    uchar buf[1024];

    if (n_param == 0 || tk->kind != TK_identifier)
        return 0;

    len = cpp_token_splice(tk, buf, sizeof(buf));
    name = string_ref_newlen((const char *)buf, len);

    for (i = 0; i < n_param; i++) {
        if (param[i] == name)
            return 1;
    }
    return 0;
}

static uint parse_macro_param(cpp_context *ctx, cpp_token *tk,
                              string_ref **param)
{
    uchar first;
    uchar buf[1024];
    uint n, len, cap;
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
                p = realloc(p, cap);
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
        len = cpp_token_splice(tk, buf, sizeof(buf));
        p[n++] = string_ref_newlen((const char *)buf, len);
        cpp_next(ctx, tk);
        first = 0;
    }

    cpp_next(ctx, tk);

    *param = p;
    return n;
}

static void parse_macro_arg(cpp_context *ctx, cpp_token *tk, string_ref param,
                            ht_t *args)
{
    uchar kind;
    int paren = 0;
    cpp_macro_arg *arg = macro_arg_new(param);

    while (1) {
        if (paren == 0 && tk->kind == ')') {
            break;
        } else if (paren == 0 && param != g__VA_ARGS__ && tk->kind == ',') {
            break;
        } else if (tk->kind == TK_eof) {
            hash_table_cleanup_with_free(args, macro_arg_free);
            cpp_error(ctx, tk, "unexpected end of file");
        }
        if (tk->kind == '(')
            paren++;
        else if (tk->kind == ')')
            paren--;
        tk->wscount = !!tk->wscount;
        cpp_token_array_append(&arg->body, tk);
        cpp_next_nonl(ctx, tk);
    }

    kind = tk->kind;
    tk->kind = TK_eof;
    tk->length = 0;
    cpp_token_array_append(&arg->body, tk);
    tk->kind = kind;

    hash_table_insert(args, param, arg);
}

static void collect_args(cpp_context *ctx, cpp_macro *m, cpp_token *tk,
                         ht_t *args)
{
    uchar first = 1;
    string_ref *param = m->param;
    uint i = 0, n_param = m->n_param;

    hash_table_setup(args, n_param);
    cpp_next_nonl(ctx, tk);

    while (i < n_param) {
        if (!first) {
            if (tk->kind != ',') {
                hash_table_cleanup_with_free(args, macro_arg_free);
                cpp_error(ctx, tk, "too few arguments for macro '%s'",
                                    string_ref_ptr(m->name));
            }
            cpp_next_nonl(ctx, tk);
        }
        parse_macro_arg(ctx, tk, param[i++], args);
        first = 0;
    }

    if (tk->kind != ')') {
        hash_table_cleanup_with_free(args, macro_arg_free);
        cpp_error(ctx, tk, "too many arguments for macro '%s'",
                            string_ref_ptr(m->name));
    }
}

static const char *month[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

static cpp_token *expand_line(cpp_context *ctx, cpp_token *tk)
{
    uchar kind;
    cpp_token_array_clear(&ctx->line);

    while (1) {
        if (tk->kind == '\n' || tk->kind == TK_eof)
            break;
        else if (tk->kind == TK_identifier && expand(ctx, tk))
            ;
        else
            cpp_token_array_append(&ctx->line, tk);
        cpp_next(ctx, tk);
    }

    kind = tk->kind;
    tk->kind = TK_eof;
    cpp_token_array_append(&ctx->line, tk);
    tk->kind = kind;

    assert(ctx->file_macro == NULL);
    return ctx->line.tokens;
}

static void expand_builtin(cpp_context *ctx, string_ref name,
                           cpp_token *macro_tk)
{
    time_t now;
    int len = 0;
    uchar dt = 0;
    struct tm *tm;
    char buf[4096];
    struct stat sb;

    if (name == g__FILE__) {
        len = snprintf(buf, sizeof(buf), "\"%s\"", ctx->stream->ppfname);
        macro_tk->kind = TK_string;
    } else if (name == g__LINE__) {
        len = snprintf(buf, sizeof(buf), "%d", ctx->stream->pplineno ?
                                               ctx->stream->pplineno :
                                               ctx->stream->lineno);
        macro_tk->kind = TK_number;
    } else if (name == g__COUNTER__) {
        len = snprintf(buf, sizeof(buf), "%d", ctx->ppcounter++);
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
            ctx->ppdate = cpp_buffer_append(ctx, (uchar *)buf, len + 1);
        } else {
            len = strlen((const char *)ctx->ppdate);
        }
        dt = 1;
        macro_tk->p = ctx->ppdate;
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
            ctx->pptime = cpp_buffer_append(ctx, (uchar *)buf, len + 1);
        } else {
            len = strlen((const char *)ctx->pptime);
        }
        dt = 1;
        macro_tk->p = ctx->pptime;
        macro_tk->kind = TK_string;
    } else {
        cpp_error(ctx, macro_tk, "unhandled builtin macro '%s'",
                                  string_ref_ptr(name));
    }

    if (!dt)
        macro_tk->p = cpp_buffer_append(ctx, (uchar *)buf, len);

    macro_tk->length = len;
    macro_tk->flags &= ~CPP_TOKEN_ESCNL;
}

static void stringize(cpp_context *ctx, cpp_token_array *os, cpp_token *arg_tk,
                      cpp_token_array *_is)
{
    cpp_token tmp;
    uchar first = 1;
    cpp_stream stream;
    const cpp_token *is = _is->tokens;
    const uchar *p = cpp_buffer_append_ch(ctx, '"');

    while (is->kind != TK_eof) {
        if (!first && (is->wscount > 0 || HAS_FLAG(is->flags, CPP_TOKEN_BOL)))
            cpp_buffer_append_ch(ctx, ' ');
        first = 0;
        if (is->kind == TK_string || is->kind == TK_char_const) {
            uint i;
            const uchar *tp = is->p;
            for (i = 0; i < is->length; i++) {
                if (tp[i] == '"' || tp[i] == '\\')
                    cpp_buffer_append_ch(ctx, '\\');
                cpp_buffer_append_ch(ctx, tp[i]);
            }
        } else {
            cpp_buffer_append(ctx, is->p, is->length);
        }
        is++;
    }

    cpp_buffer_append_ch(ctx, '"');

    stream.flags = 0;
    stream.lineno = ctx->stream->lineno;
    stream.pplineno = ctx->stream->pplineno;
    stream.fname = ctx->stream->fname;
    stream.ppfname = ctx->stream->ppfname;
    stream.file = ctx->stream->file;
    stream.p = p;
    stream.cond = NULL;
    stream.prev = NULL;

    cpp_lex_scan(&stream, &tmp);
    tmp.wscount = !!arg_tk->wscount;

    cpp_token_array_append(os, &tmp);
}

static void paste(cpp_context *ctx, cpp_token_array *os, cpp_token *rhs,
                  cpp_token *macro_tk)
{
    ushort wscount;
    int len, len2, n;
    cpp_stream stream; /* fake stream */
    cpp_token tmp, *lhs;
    char buf[1024], buf2[1024], buf3[2049] = {0};

    lhs = &os->tokens[os->n - 1];
    wscount = !!lhs->wscount;
    len = (int)cpp_token_splice(lhs, (uchar *)buf, sizeof(buf));
    len2 = (int)cpp_token_splice(rhs, (uchar *)buf2, sizeof(buf2));
    n = snprintf(buf3, 2048, "%.*s%.*s", len, buf, len2, buf2);

    stream.flags = 0;
    stream.lineno = ctx->stream->lineno;
    stream.pplineno = ctx->stream->pplineno;
    stream.fname = ctx->stream->fname;
    stream.ppfname = ctx->stream->ppfname;
    stream.p = cpp_buffer_append(ctx, (const uchar *)buf3, n + 1);
    stream.file = ctx->stream->file;
    stream.cond = NULL;
    stream.prev = NULL;

    cpp_lex_scan(&stream, &tmp);
    if (tmp.kind == TK_eof)
        cpp_error(ctx, macro_tk, "## produced invalid pp-token '%s'", buf3);

    *lhs = tmp;
    lhs->wscount = wscount;

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
        } else if (tk.kind == TK_identifier && expand(ctx, &tk)) {
            ;
        } else {
            tk.flags &= ~CPP_TOKEN_BOL;
            cpp_token_array_append(os, &tk);
        }
    }

    if (i < os->n) {
        if (HAS_FLAG(param_tk->flags, CPP_TOKEN_BOL))
            os->tokens[i].flags |= CPP_TOKEN_BOL;
        if (HAS_FLAG(param_tk->flags, CPP_TOKEN_ESCNL))
            os->tokens[i].flags |= CPP_TOKEN_ESCNL;
        os->tokens[i].wscount = param_tk->wscount;
        os->tokens[i].lineno = param_tk->lineno;
    }

    arg_stream_pop(ctx);
}

static cpp_macro_arg *find_arg(ht_t *args, cpp_token *tk)
{
    uint len;
    uchar buf[1024];
    string_ref name;

    if (tk->kind != TK_identifier || args == NULL || args->count == 0)
        return NULL;

    len = cpp_token_splice(tk, buf, sizeof(buf));
    name = string_ref_newlen((const char *)buf, len);
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

        /* Need to suppress macro expansion. */
        if (arg != NULL && is[1].kind == TK_paste) {
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
                while (is2->kind != TK_eof)
                    cpp_token_array_append(os, is2++);
                is++; /* Handle ## in the next iteration */
            }
            continue;
        }

        /* Handle argument. */
        if (arg != NULL) {
            expand_arg(ctx, arg, os, is++);
            continue;
        }

        /* Remaining token from the replacement list. */
        is->flags &= ~CPP_TOKEN_BOL;
        cpp_token_array_append(os, is++);
    }

    cpp_token_array_append(os, is); /* TK_eom */
}

static uchar expand(cpp_context *ctx, cpp_token *tk)
{
    uint i;
    uchar buf[1024];
    uint len = cpp_token_splice(tk, buf, sizeof(buf));
    string_ref name = string_ref_newlen((const char *)buf, len);
    cpp_macro *m = hash_table_lookup(&ctx->macro, name);
    if (m == NULL)
        return 0;

    if (HAS_FLAG(m->flags, CPP_MACRO_BUILTIN)) {
        expand_builtin(ctx, name, tk);
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
    } else {
        macro_stack_push(ctx, name);
        ms = ctx->argstream ? ctx->argstream->macro : ctx->file_macro;
        subst(ctx, m, &macro_tk, NULL, &ms->tok);
    }

    assert(ms);
    ms->p = ms->tok.tokens;

    if (HAS_FLAG(macro_tk.flags, CPP_TOKEN_BOL))
        ms->tok.tokens[0].flags |= CPP_TOKEN_BOL;
    if (HAS_FLAG(macro_tk.flags, CPP_TOKEN_ESCNL))
        ms->tok.tokens[0].flags |= CPP_TOKEN_ESCNL;
    ms->tok.tokens[0].wscount = macro_tk.wscount;
    ms->tok.tokens[0].lineno = macro_tk.lineno;
    return 1;
}

static void do_define(cpp_context *ctx, cpp_token *tk)
{
    uchar buf[1024];
    uchar flags = 0;
    cpp_macro *m, *m2;
    cpp_token_array body;
    uint len, n_param = 0;
    string_ref name, *param = NULL;

    cpp_next(ctx, tk);
    if (tk->kind != TK_identifier)
        cpp_error(ctx, tk, "no macro name given in #define");

    len = cpp_token_splice(tk, buf, sizeof(buf));
    name = string_ref_newlen((const char *)buf, len);
    if (name >= g__VA_ARGS__ && name <= g_defined) {
        if (name == g_defined)
            cpp_error(ctx, tk, "'defined' cannot be used as a macro name");
        else if (name == g__VA_ARGS__)
            cpp_warn(ctx, tk, "__VA_ARGS__ used as a macro name has no effect");
    }

    m2 = hash_table_lookup(&ctx->macro, name);
    cpp_next(ctx, tk);

    if (tk->kind == '(' && tk->wscount == 0) {
        n_param = parse_macro_param(ctx, tk, &param);
        flags = CPP_MACRO_FUNC;
    }

    cpp_token_array_setup(&body, 8);

    while (tk->kind != '\n' && tk->kind != TK_eof) {
        tk->flags &= ~CPP_TOKEN_BOL;
        tk->wscount = !!tk->wscount;
        if (tk->kind == '#' && HAS_FLAG(flags, CPP_MACRO_FUNC)) {
            cpp_token_array_append(&body, tk); /* append # */
            cpp_next(ctx, tk);
            if (!find_param(param, n_param, tk)) {
                free(param);
                cpp_token_array_cleanup(&body);
                cpp_error(ctx, tk, "'#' is not followed by a macro parameter");
            }
            cpp_token_array_append(&body, tk); /* append param */
        } else if (tk->kind == TK_paste) {
            if (body.n == 0) {
                free(param);
                cpp_token_array_cleanup(&body);
                cpp_error(ctx, tk, "'##' cannot appear at either end of replacement list");
            }
            cpp_token_array_append(&body, tk); /* append ## */
            cpp_next(ctx, tk);
            if (tk->kind == '\n' || tk->kind == TK_eof) {
                free(param);
                cpp_token_array_cleanup(&body);
                cpp_error(ctx, tk, "'##' cannot appear at either end of replacement list");
            }
            cpp_token_array_append(&body, tk); /* append rhs */
        } else {
            cpp_token_array_append(&body, tk);
        }
        cpp_next(ctx, tk);
    }

    tk->kind = TK_eom;
    cpp_token_array_append(&body, tk);

    m = macro_new(name, flags, body);
    if (HAS_FLAG(flags, CPP_MACRO_FUNC)) {
        m->param = param;
        m->n_param = n_param;
    }

    if (m2 != NULL)
        cpp_warn(ctx, tk, "'%s' redefined", string_ref_ptr(name));

    if (!HAS_FLAG(flags, CPP_MACRO_FUNC)) {
        if (body.n > 1 && body.tokens[0].wscount == 0)
            cpp_warn(ctx, tk, "no whitespace after macro name");
    }

    hash_table_insert(&ctx->macro, name, m);
}

static void do_undef(cpp_context *ctx, cpp_token *tk)
{
    uint len;
    cpp_macro *m;
    uchar buf[1024];
    string_ref name;

    cpp_next(ctx, tk);
    if (tk->kind != TK_identifier)
        cpp_error(ctx, tk, "no macro name given in #undef");

    len = cpp_token_splice(tk, buf, sizeof(buf));
    name = string_ref_newlen((const char *)buf, len);
    if (name >= g__VA_ARGS__ && name <= g_defined) {
        if (name == g_defined)
            cpp_error(ctx, tk, "'defined' cannot be used as a macro name");
        else if (name == g__VA_ARGS__)
            cpp_warn(ctx, tk, "__VA_ARGS__ used as a macro name has no effect");
        else
            cpp_warn(ctx, tk, "undefining '%s'", string_ref_ptr(name));
    }

    m = hash_table_remove(&ctx->macro, name);
    if (m != NULL)
        macro_free((void *)m);

    cpp_next(ctx, tk);
    if (tk->kind != '\n')
        cpp_error(ctx, tk, "stray token after #undef");
}

/* ------------------------------------------------------------------------ */

static void skip_line(cpp_context *ctx, cpp_token *tk)
{
    do {
        cpp_next(ctx, tk);
    } while (tk->kind != '\n');
}

static cpp_directive_kind get_directive_kind(const uchar *buf, uint len)
{
    cpp_directive_kind kind = CPP_DIR_UNKNOWN;

    switch (len) {
    case 7:
        if (memcmp(buf, "include", 7) == 0)
            kind = CPP_DIR_INCLUDE;
        break;
    case 6:
        if (memcmp(buf, "define", 6) == 0)
            kind = CPP_DIR_DEFINE;
        else if (memcmp(buf, "ifndef", 6) == 0)
            kind = CPP_DIR_IFNDEF;
        else if (memcmp(buf, "pragma", 6) == 0)
            kind = CPP_DIR_PRAGMA;
        break;
    case 5:
        if (memcmp(buf, "endif", 5) == 0)
            kind = CPP_DIR_ENDIF;
        else if (memcmp(buf, "error", 5) == 0)
            kind = CPP_DIR_ERROR;
        else if (memcmp(buf, "ifdef", 5) == 0)
            kind = CPP_DIR_IFDEF;
        else if (memcmp(buf, "undef", 5) == 0)
            kind = CPP_DIR_UNDEF;
        break;
    case 4:
        if (memcmp(buf, "elif", 4) == 0)
            kind = CPP_DIR_ELIF;
        else if (memcmp(buf, "else", 4) == 0)
            kind = CPP_DIR_ELSE;
        else if (memcmp(buf, "line", 4) == 0)
            kind = CPP_DIR_LINE;
        break;
    case 2:
        if (memcmp(buf, "if", 2) == 0)
            kind = CPP_DIR_IF;
    default:
        break;
    }

    return kind;
}

static uchar is_hash(cpp_context *ctx, cpp_token *tk)
{
    return AT_BOL(tk) && tk->kind == '#' && ctx->file_macro == NULL;
}

/* Advance next token and run the preprocessor and do macro expansion if
 * necessary. */
static void cpp_preprocess(cpp_context *ctx, cpp_token *tk)
{
    uint len;
    uchar buf[32];
    cpp_token hash;
    cpp_directive_kind dkind;

    while (1) {
        cpp_next(ctx, tk);
        if (tk->kind == TK_eof) {
            cpp_stream_pop(ctx);
            if (ctx->stream == NULL)
                return;
            continue;
        } else if (tk->kind == '\n') {
            continue;
        } else if (tk->kind == TK_identifier && expand(ctx, tk)) {
            continue;
        } else if (!is_hash(ctx, tk)) {
            return;
        }

        hash = *tk;
        cpp_next(ctx, tk);

        if (tk->kind == '\n')
            continue;

        len = cpp_token_splice(tk, buf, sizeof(buf));
        dkind = get_directive_kind(buf, len);

        switch (dkind) {
        case CPP_DIR_INCLUDE:
            do_include(ctx, tk);
            break;
        case CPP_DIR_IF:
        case CPP_DIR_IFDEF:
        case CPP_DIR_IFNDEF:
        case CPP_DIR_ELIF:
        case CPP_DIR_ELSE:
        case CPP_DIR_ENDIF:
            skip_line(ctx, tk);
            break;
        case CPP_DIR_DEFINE:
            do_define(ctx, tk);
            break;
        case CPP_DIR_UNDEF:
            do_undef(ctx, tk);
            break;
        case CPP_DIR_LINE:
            do_line(ctx, tk);
            break;
        case CPP_DIR_PRAGMA:
            skip_line(ctx, tk);
            break;
        case CPP_DIR_ERROR:
            do_error(ctx, tk);
            break;
        default:
            cpp_error(ctx, tk, "unknown directive '%.*s'", len, (const char *)buf);
            break;
        }
    }
}
