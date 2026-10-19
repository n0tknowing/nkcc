/* vim: set expandtab ts=4 sw=4 tw=0 : */

/*
 * token.c - utilities for cpp_token
 *
 * Copyright (C) 2026  Widianto Nur F <xnaltasee@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <assert.h>
#include <string.h>
#include "cpp.h"

const char *cpp_token_kind(uchar kind)
{
    if (kind < 128 || (kind >= TK_elipsis && kind <= TK_asg_bxor) ||
        kind == TK_paste)
        return "Punctuator";
    else if (kind == TK_identifier)
        return "Identifier";
    else if (kind == TK_number)
        return "Number";
    else if (kind == TK_string)
        return "String literal";
    else if (kind == TK_char_const)
        return "Character constant";
    else if (kind == TK_eof)
        return "End of file";
    else
        return "???";
}

uint cpp_token_spell(const cpp_token *tk, uchar *buf, uint bufsz)
{
    uint i = 0, j = 0, len;

    if (tk->kind == TK_identifier) {
        len = MIN(string_ref_len(tk->p.ref), bufsz);
        memcpy(buf, (const uchar *)string_ref_ptr(tk->p.ref), len);
    } else {
        const uchar *p = cpp_token_pointer(tk);
        if (HAS_FLAG(tk->flags, CPP_TOKEN_ESCNL)) {
            uint len2 = tk->p.src.length;
            while (i < len2 && j < bufsz) {
                if (i + 2 <= len2 && p[i] == '\\' && p[i+1] == '\n')
                    i += 2;
                else if (i + 3 <= len2 && p[i] == '\\' && p[i+1] == '\r' && p[i+2] == '\n')
                    i += 3;
                else
                    buf[j++] = p[i++];
            }
            len = j;
        } else {
            len = MIN(tk->p.src.length, bufsz);
            memcpy(buf, p, len);
        }
    }

    return i ? j : len;
}

bool cpp_token_equal(const cpp_token *tk1, const cpp_token *tk2)
{
    uint len1, len2;
    uchar buf1[4096], buf2[4096];

    if (tk1->kind != tk2->kind)
        return false;

    uchar spc1 = HAS_FLAG(tk1->flags, CPP_TOKEN_SPACE);
    uchar spc2 = HAS_FLAG(tk2->flags, CPP_TOKEN_SPACE);

    if (spc1 != spc2)
        return false;

    switch (tk1->kind) {
    case TK_identifier:
        if (tk1->p.ref != tk2->p.ref)
            return false;
        break;
    case TK_string:
    case TK_char_const:
    case TK_number:
        len1 = cpp_token_spell(tk1, buf1, sizeof(buf1));
        len2 = cpp_token_spell(tk2, buf2, sizeof(buf2));
        if (len1 != len2 || memcmp(buf1, buf2, len1) != 0)
            return false;
        break;
    default:
        break;
    }

    return true;
}

void cpp_token_print(FILE *fp, const cpp_token *tk)
{
    uint len;
    const uchar *p;
    uchar buf[4096];

    if (PREV_SPACE(tk))
        fwrite((const void *)" ", 1, 1, fp);

    if (tk->kind == TK_identifier) {
        p = (const uchar *)string_ref_ptr(tk->p.ref);
        len = string_ref_len(tk->p.ref);
    } else if (unlikely(HAS_FLAG(tk->flags, CPP_TOKEN_ESCNL))) {
        len = cpp_token_spell(tk, buf, sizeof(buf));
        p = buf;
    } else {
        p = cpp_token_pointer(tk);
        len = tk->p.src.length;
    }

    fwrite(p, 1, len, fp);
}
