// Lexer implementation
/*
 * this comment is intentionally placed here to\
 * stress the lexer and the preprocessor
 * */
/**/#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpp.h"

static cpp_context *g_context;

/* handle complicated "\\\n" */
#define CHECK_ESCNL(_s, _t) do {                                \
        if (unlikely(*(_s)->p == '\\' && (_s)->p[1] == '\n')) { \
            (_s)->p += 2; (_s)->lineno++;                       \
            (_t)->flags |= CPP_TOKEN_ESCNL;                     \
        }                                                       \
    } while (0)


/* ------------------------------------------------------------------------- */

void cpp_lex_setup(cpp_context *ctx)
{
    g_context = ctx;
}

void cpp_lex_cleanup(void)
{
}

/* ---- locale-free ctype.h ------------------------------------------------ */

static int isodigit(int ch)
{
    return (uint)ch - '0' < 8;
}

static int isdigit(int ch)
{
    return (uint)ch - '0' < 10;
}

static int isxdigit(int ch)
{
    return isdigit(ch) || ((uint)ch | 32) - 'a' < 6;
}

static int isupper(int ch)
{
    return (uint)ch - 'A' < 26;
}

static int islower(int ch)
{
    return (uint)ch - 'a' < 26;
}

static int isalpha(int ch)
{
    return islower(ch) || isupper(ch);
}

static int isalnum(int ch)
{
    return isalpha(ch) || isdigit(ch);
}

static int isspace(int ch)
{
    return ch == ' ' || (uint)ch - '\t' < 5;
}

static int ispunct(int ch)
{
    return ((uint)ch - 33 < 94) && !isalnum(ch);
}

static int tolower(int ch)
{
    return isupper(ch) ? ch | 32 : ch;
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
    s->p += 2;

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
    tk->p = s->p;

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
        tk->length = (uint)(s->p - tk->p);
    }
}

static void cpp_lex_punct(cpp_stream *s, cpp_token *tk)
{
    tk->lineno = s->lineno;
    tk->kind = *s->p;
    tk->p = s->p;
    s->p++;

    /* one character, no need to handle line continuation */
    if (*s->p != '\\' && !ispunct(*s->p)) {
        tk->length = 1;
        return;
    }

    CHECK_ESCNL(s, tk);

    switch (*tk->p) {
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
            tk->length = (uint)(s->p - tk->p);
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
            tk->length = (uint)(s->p - tk->p);
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
                tk->length = (uint)(s->p - tk->p);
            }
        }
        break;
    }

    if (tk->kind >= TK_elipsis) {
        if (tk->kind != TK_elipsis) s->p++;
        tk->length = (uint)(s->p - tk->p);
    } else {
        /* restore line */
        s->p = tk->p; s->lineno = tk->lineno; s->p++;
        tk->length = 1; tk->kind = *tk->p;
    }
}

static void cpp_lex_ident(cpp_stream *s, cpp_token *tk)
{
    tk->p = s->p;
    tk->lineno = s->lineno;
    s->p++;

    while (*s->p) {
        CHECK_ESCNL(s, tk);
        if (!(isalnum(*s->p) || *s->p == '_'))
            break;
        s->p++;
    }

    tk->length = (uint)(s->p - tk->p);
}

static void cpp_lex_number(cpp_stream *s, cpp_token *tk)
{
    tk->p = s->p;
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
            tk->flags |= CPP_TOKEN_FLNUM;
            s->p++;
            CHECK_ESCNL(s, tk);
            if (*s->p == '+' || *s->p == '-')
                s->p++;
        } else if (!isalnum(*s->p)) {
            break;
        } else {
            s->p++;
        }
    }

    tk->length = (uint)(s->p - tk->p);
}

void cpp_lex_scan(cpp_stream *s, cpp_token *tk)
{
    if (unlikely(s == NULL))
        return;

    tk->flags = s->flags;
    s->flags = 0;
    tk->wscount = 0;
    tk->fileno = s->file->id;

    while (*s->p) {
        /* line continuation */
        if (*s->p == '\\' && s->p[1] == '\n') {
            s->p += 2; s->lineno++;
            continue;
        }

        /* comment */
        if (*s->p == '/') {
            /* special line continuation handling */
            if (s->p[1] == '\\' && s->p[2] == '\n' &&
               (s->p[3] == '/' || s->p[3] == '*')) {
                s->p++;
                s->lineno++;
                cpp_lex_comment(s, s->p[2]);
                continue;
            } else if (s->p[1] == '/' || s->p[1] == '*') {
                cpp_lex_comment(s, s->p[1]);
                continue;
            }
        }

        /* newline */
        if (*s->p == '\n') {
            s->p++; tk->lineno = s->lineno++;
            s->flags = tk->flags | CPP_TOKEN_BOL;
            tk->kind = '\n'; tk->length = 0;
            return;
        }

        /* whitespace */
        if (isspace(*s->p)) {
            /* convert tab into 2 spaces */
            if (*s->p == '\t')
                tk->wscount++;
            tk->wscount++;
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
        if (isdigit(*s->p) || (*s->p == '.' && isdigit(s->p[1]))) {
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

        /* punctuator */
        cpp_lex_punct(s, tk);
        return;
    }

    tk->lineno = s->lineno;
    tk->kind = TK_eof;
    tk->length = 0;
    tk->p = s->p;
}
