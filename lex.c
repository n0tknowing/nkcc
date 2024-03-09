#include "cpp.h"

/* handle complicated "\\\n" */
#define CHECK_ESCNL(_s, _t) do {                                \
        if (unlikely(*(_s)->p == '\\' && (_s)->p[1] == '\n')) { \
            (_s)->p += 2; (_s)->lineno++;                       \
            (_t)->flags |= CPP_TOKEN_ESCNL;                     \
        }                                                       \
    } while (0)


/* ------------------------------------------------------------------------- */

static cpp_context *g_context;
static cpp_buffer g_lexbuf;

void cpp_lex_setup(cpp_context *ctx)
{
    g_context = ctx;
    cpp_buffer_setup(&g_lexbuf, 16384);
}

void cpp_lex_cleanup(cpp_context *ctx)
{
    (void)ctx;
    cpp_buffer_cleanup(&g_lexbuf);
}

/* ------------------------------------------------------------------------- */

static void cpp_lex_error(cpp_stream *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "\x1b[1;29m%s:%u:\x1b[0m ", s->fname, s->lineno);
    fprintf(stderr, "\x1b[1;31merror:\x1b[0m ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    cpp_context_cleanup(g_context);
    exit(1);
}

static void cpp_lex_comment(cpp_stream *s, tkchar kind)
{
    uint start = s->lineno;
    s->p++;

    if (kind == '/') {
        while (*s->p) {
            if (*s->p == '\\' && s->p[1] == '\n') {
                s->p += 2;
                s->lineno++;
                continue;
            }
            if (*s->p == '\n')
                return;
            s->p++;
        }
    } else {
        int maybe_done = 0;
        while (*s->p) {
            if (*s->p == '\\' && s->p[1] == '\n') {
                s->p += 2;
                s->lineno++;
                continue;
            }
            if (*s->p == '/' && maybe_done) {
                s->p++;
                return;
            }
            maybe_done = 0;
            if (*s->p == '\n')
                s->lineno++;
            else if (*s->p == '*')
                maybe_done = 1;
            s->p++;
        }
    }

    s->lineno = start;
    cpp_lex_error(s, "unterminated comment");
}

void cpp_lex_string(cpp_stream *s, cpp_token *tk, tkchar endq)
{
    tk->lineno = s->lineno;
    tk->p.ptr = s->p;

    if (endq == '"' || endq == '\'')
        s->p++;

    while (1) {
        CHECK_ESCNL(s, tk);
        if (*s->p == endq || !*s->p || *s->p == '\n')
            break;
        if (*s->p == '\\') {
            s->p++;
            CHECK_ESCNL(s, tk);
            if (isodigit(*s->p)) {
                s->p++;
                do {
                    CHECK_ESCNL(s, tk);
                    if (!isodigit(*s->p))
                        break;
                } while (*++s->p);
            } else if (*s->p == 'x') {
                s->p++;
                do {
                    CHECK_ESCNL(s, tk);
                    if (!isxdigit(*s->p))
                        break;
                } while (*++s->p);
            } else if (strchr("\"\\'?abfnrtv", *s->p)) {
                s->p++;
            }
        } else {
            s->p++;
        }
    }

    if (!*s->p || *s->p == '\n') {
        cpp_lex_error(s, "missing terminating %c character", endq);
    } else {
        s->p++;
        tk->length = (uint)(s->p - tk->p.ptr);
    }
}

static void cpp_lex_punct(cpp_stream *s, cpp_token *tk)
{
    tk->lineno = s->lineno;
    tk->p.ptr = s->p;
    tk->kind = *s->p;
    s->p++;

    /* one character, no need to handle line continuation */
    if (*s->p != '\\' && !ispunct(*s->p)) {
        tk->length = 1;
        return;
    }

    CHECK_ESCNL(s, tk);

    switch (*(tk->p.ptr)) {
    case '+':
        if (*s->p == '+') tk->kind = TK_incr;
        else if (*s->p == '=') tk->kind = TK_asg_add;
        break;
    case '-':
        if (*s->p == '-') tk->kind = TK_decr;
        else if (*s->p == '=') tk->kind = TK_asg_sub;
        else if (*s->p == '>') tk->kind = TK_arrow;
        break;
    case '*':
        if (*s->p == '=') tk->kind = TK_asg_mul;
        break;
    case '/':
        if (*s->p == '=') tk->kind = TK_asg_div;
        break;
    case '%':
        if (*s->p == '=') tk->kind = TK_asg_mod;
        break;
    case '&':
        if (*s->p == '&') tk->kind = TK_and;
        else if (*s->p == '=') tk->kind = TK_asg_band;
        break;
    case '|':
        if (*s->p == '|') tk->kind = TK_or;
        else if (*s->p == '=') tk->kind = TK_asg_bor;
        break;
    case '=':
        if (*s->p == '=') tk->kind = TK_eq;
        break;
    case '!':
        if (*s->p == '=') tk->kind = TK_ne;
        break;
    case '^':
        if (*s->p == '&') tk->kind = TK_asg_bxor;
        break;
    case '#':
        if (*s->p == '#') tk->kind = TK_paste;
        break;
    case '<':
        if (*s->p == '<') {
            s->p++;
            CHECK_ESCNL(s, tk);
            if (*s->p == '=') {
                s->p++;
                tk->kind = TK_asg_lshift;
            } else {
                tk->kind = TK_lshift;
            }
            tk->length = (uint)(s->p - tk->p.ptr);
            return;
        } else if (*s->p == '=') {
            tk->kind = TK_le;
        }
        break;
    case '>':
        if (*s->p == '>') {
            s->p++;
            CHECK_ESCNL(s, tk);
            if (*s->p == '=') {
                s->p++;
                tk->kind = TK_asg_rshift;
            } else {
                tk->kind = TK_rshift;
            }
            tk->length = (uint)(s->p - tk->p.ptr);
            return;
        } else if (*s->p == '=') {
            tk->kind = TK_ge;
        }
        break;
    case '.':
        if (*s->p == '.') {
            s->p++;
            CHECK_ESCNL(s, tk);
            if (*s->p == '.') {
                s->p++;
                tk->kind = TK_elipsis;
                tk->length = (uint)(s->p - tk->p.ptr);
            }
        }
        break;
    }

    if (tk->kind >= TK_elipsis) {
        if (tk->kind != TK_elipsis) s->p++;
        tk->length = (uint)(s->p - tk->p.ptr);
    } else {
        /* restore line */
        s->p = tk->p.ptr; s->lineno = tk->lineno; s->p++;
        tk->length = 1; tk->kind = *(tk->p.ptr);
    }
}

static void cpp_lex_ident(cpp_stream *s, cpp_token *tk)
{
    const uchar *p = cpp_buffer_append_ch(&g_lexbuf, *s->p);

    tk->lineno = s->lineno;
    s->p++;

    while (*s->p != 0) {
        CHECK_ESCNL(s, tk);
        if (!(isalnum(*s->p) || *s->p == '_'))
            break;
        cpp_buffer_append_ch(&g_lexbuf, *s->p);
        s->p++;
    }

    tk->p.ref = string_ref_newlen((const char *)p, g_lexbuf.len);
    tk->length = string_ref_len(tk->p.ref);
    cpp_buffer_clear(&g_lexbuf);
}

static void cpp_lex_number(cpp_stream *s, cpp_token *tk)
{
    tk->p.ptr = s->p;
    tk->lineno = s->lineno;

    while (*s->p) {
        CHECK_ESCNL(s, tk);
        if (*s->p == '.') {
            tk->flags |= CPP_TOKEN_FLNUM;
            s->p++;
            CHECK_ESCNL(s, tk);
            if (!isdigit(*s->p))
                break;
            s->p++;
        } else if (tolower(*s->p) == 'e' || tolower(*s->p) == 'p') {
            s->p++;
            CHECK_ESCNL(s, tk);
            if (*s->p == '+' || *s->p == '-') {
                tk->flags |= CPP_TOKEN_FLNUM;
                s->p++;
            }
        } else if (!isalnum(*s->p)) {
            break;
        } else {
            s->p++;
        }
    }

    tk->length = (uint)(s->p - tk->p.ptr);
}

void cpp_lex_scan(cpp_stream *s, cpp_token *tk)
{
    const uchar *p;

    if (unlikely(s == NULL))
        return;

    tk->flags = s->flags;
    s->flags = 0;
    tk->fileno = s->file->no;

    while (*s->p) {
        /* line continuation */
        if (*s->p == '\\' && s->p[1] == '\n') {
            s->p += 2; s->lineno++;
            continue;
        }

        /* comment */
        if (*s->p == '/') {
            /* a complicated line continuation handling */
            p = s->p++;
            if (*s->p == '\\' && s->p[1] == '\n') {
                do {
                    s->p += 2;
                    s->lineno++;
                } while (*s->p == '\\' && s->p[1] == '\n');
            }
            if (*s->p == '/' || *s->p == '*') {
                tk->flags |= CPP_TOKEN_SPACE;
                cpp_lex_comment(s, *s->p);
                continue;
            } else {
                /* restore, line continuations can appear anywhere */
                s->p = p;
            }
        }

        /* newline */
        if (*s->p == '\n') {
            s->p++; tk->lineno = s->lineno++;
            s->flags = tk->flags | CPP_TOKEN_BOL;
            s->flags &= ~CPP_TOKEN_SPACE;
            tk->kind = '\n'; tk->length = 0;
            return;
        }

        /* whitespace */
        if (isspace(*s->p)) {
            tk->flags |= CPP_TOKEN_SPACE;
            s->p++;
            continue;
        }

        /* identifier */
        if (isalpha(*s->p) || *s->p == '_') {
            cpp_lex_ident(s, tk);
            tk->kind = TK_identifier;
            return;
        }

        /* number */
        if (isdigit(*s->p)) {
            cpp_lex_number(s, tk);
            tk->kind = TK_number;
            return;
        }

        /* string literal or character constant */
        if (*s->p == '"' || *s->p == '\'') {
            tkchar ch = *s->p;
            cpp_lex_string(s, tk, ch);
            tk->kind = ch == '"' ? TK_string : TK_char_const;
            return;
        }

        /* number or punctuator */
        if (*s->p == '.') {
            p = s->p++;
            CHECK_ESCNL(s, tk);
            if (isdigit(*s->p)) {
                s->p = p;
                cpp_lex_number(s, tk);
                tk->kind = TK_number;
                return;
            }
            /* else fallthrough to scan punctuator */
            s->p = p;
        }

        /* punctuator */
        cpp_lex_punct(s, tk);
        return;
    }

    tk->lineno = s->lineno;
    tk->kind = TK_eof;
    tk->length = 0;
    tk->p.ptr = s->p;
}
