#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "cpp.h"

const char *cpp_token_kind(uchar kind)
{
    if (kind < 128 || (kind >= TK_elipsis && kind <= TK_asg_bxor) ||
        kind == TK_paste)
        return "OPR";
    else if (kind == TK_identifier)
        return "IDN";
    else if (kind == TK_number)
        return "NUM";
    else if (kind == TK_string)
        return "STR";
    else if (kind == TK_char_const)
        return "CHR";
    else
        return "???";
}

uint cpp_token_splice(cpp_token *tk, uchar *buf, uint bufsz)
{
    uint i = 0, j = 0;
    const uchar *p = tk->p;
    uint len = MIN(tk->length, bufsz);

    if (HAS_FLAG(tk->flags, CPP_TOKEN_ESCNL)) {
        while (i < len) {
            if (i + 2 <= len && !memcmp(p + i, "\\\n", 2))
                i += 2;
            else
                buf[j++] = p[i++];
        }
    } else {
        memcpy(buf, p, len);
    }

    return i ? j : len;
}

void cpp_token_print(FILE *fp, cpp_token *tk)
{
    uint i;
    uchar buf[1024] = {0};

    for (i = 0; i < tk->wscount; i++)
        buf[i] = ' ';

    fwrite(buf, 1, i, fp);

    if (tk->kind < 128) {
        fwrite(tk->p, 1, 1, fp);
        return;
    }

    if (unlikely(HAS_FLAG(tk->flags, CPP_TOKEN_ESCNL))) {
        uint len = cpp_token_splice(tk, buf, sizeof(buf));
        fwrite(buf, 1, len, fp);
    } else {
        fwrite(tk->p, 1, tk->length, fp);
    }
}

void cpp_token_array_setup(cpp_token_array *ts, uint max)
{
    ts->tokens = malloc(max * sizeof(cpp_token));
    assert(ts->tokens);
    ts->n = 0;
    ts->max = max;
}

void cpp_token_array_cleanup(cpp_token_array *ts)
{
    if (ts != NULL && ts->tokens != NULL) {
        free(ts->tokens); ts->tokens = NULL;
        ts->n = ts->max = 0;
    }
}

void cpp_token_array_clear(cpp_token_array *ts)
{
    ts->n = 0;
}

void cpp_token_array_move(cpp_token_array *dts, cpp_token_array *sts)
{
    uint i;
    for (i = 0; i < sts->n; i++)
        cpp_token_array_append(dts, &sts->tokens[i]);
    cpp_token_array_clear(sts);
}

void cpp_token_array_append(cpp_token_array *ts, cpp_token *tk)
{
    if (ts->n < ts->max) {
        ts->tokens[ts->n++] = *tk;
    } else {
        ts->max *= 2;
        ts->tokens = realloc(ts->tokens, ts->max * sizeof(cpp_token));
        assert(ts->tokens);
        ts->tokens[ts->n++] = *tk;
    }
}
