/*
 *
 *
 *
 *
 *
 *
 *
 *
 */

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ctype.h"
#include "strbuff.h"
#include "token.h"

typedef struct {
    const char *path;
    char *content;
    size_t size;
} file_t;

typedef struct {
    token_t token;
    file_t *file;
    const char *cur;
    const char *end;
    int line, col;
    strbuff_t sb;
} lexer_t;

#define is_digit(c)   (dtype_lut[(uint8_t)(c)] & (DD_))
#define is_bdigit(c)  (dtype_lut[(uint8_t)(c)] & (DB_))
#define is_odigit(c)  (dtype_lut[(uint8_t)(c)] & (DO_))
#define is_xdigit(c)  (dtype_lut[(uint8_t)(c)] & (DD_|DX_))

#define DD_   0x01
#define DB_   0x02
#define DO_   0x04
#define DX_   0x08

#define DBO_ (DD_|DB_|DO_)
#define DOO_ (DD_|DO_)

static const uint8_t dtype_lut[256] = {
    0,     0,    0,    0,    0,    0,    0,    0,
    0,     0,    0,    0,    0,    0,    0,    0,
    0,     0,    0,    0,    0,    0,    0,    0,
    0,     0,    0,    0,    0,    0,    0,    0,
    0,     0,    0,    0,    0,    0,    0,    0,
    0,     0,    0,    0,    0,    0,    0,    0,
    DBO_,  DBO_, DOO_, DOO_, DOO_, DOO_, DOO_, DOO_,
    DD_,   DD_,  0,    0,    0,    0,    0,    0,
    0,     DX_,  DX_,  DX_,  DX_,  DX_,  DX_,  0,
    0,     0,    0,    0,    0,    0,    0,    0,
    0,     0,    0,    0,    0,    0,    0,    0,
    0,     0,    0,    0,    0,    0,    0,    0,
    0,     DX_,  DX_,  DX_,  DX_,  DX_,  DX_,  0,
    0,     0,    0,    0,    0,    0,    0,    0,
    0,     0,    0,    0,    0,    0,    0,    0,
    0,     0,    0,    0,    0,    0,    0,    0,
};

// === Debugging =============================================================
// ===========================================================================

#ifdef DEBUG
#define dbg_log(...)  dbgf(__VA_ARGS__)
#else
#define dbg_log(...)
#endif

void dbgf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stdout, "\x1b[1;32m[DEBUG] ");
    vfprintf(stdout, fmt, ap);
    fprintf(stdout, "\n\x1b[0m");
    va_end(ap);
}

void msgf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    fputc('\n', stdout);
    va_end(ap);
}

// === Error reporting =======================================================
// ===========================================================================

void errf(lexer_t *lex, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    const int line = lex->token.line;
    const int pos = lex->token.col;
    const char *path = lex->file->path;

    fputc('\n', stderr);
    fprintf(stderr, "\x1b[1;31m[%s:%d:%d] error:\x1b[0m ", path, line, pos);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);

    va_end(ap);
    exit(1);
}

void warnf(lexer_t *lex, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    const int line = lex->token.line;
    const int pos = lex->token.col;
    const char *path = lex->file->path;

    fputc('\n', stderr);
    fprintf(stderr, "\x1b[1;35m[%s:%d:%d] warning:\x1b[0m ", path, line, pos);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);

    va_end(ap);
}

// === File ==================================================================
// ===========================================================================

int file_read(file_t *file, const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd == -1) return -1;

    struct stat sb;
    if (fstat(fd, &sb) == -1)
        goto close_file;

    if (!S_ISREG(sb.st_mode))
        goto close_file;

    size_t size = (size_t)sb.st_size;
    if (size == 0)
        goto close_file;

    file->content = calloc(1, size + 2ul);
    if (file->content == NULL)
        goto close_file;

    read(fd, file->content, size);
    file->size = size;
    file->path = path;
    file->content[size] = EOF;

    close(fd);
    return 0;

close_file:
    close(fd);
    return -1;
}

void file_close(file_t *file)
{
    if (file != NULL)
        free(file->content);
}

// === Lexer =================================================================
// ===========================================================================

static void lexer_next_char(lexer_t *lex)
{
    lex->cur++;
    lex->col++;
}

static void lexer_putback_char(lexer_t *lex)
{
    lex->cur--;
    lex->col--;
}

static int lexer_count_length(lexer_t *lex)
{
    return (int)(lex->cur - lex->token.lexeme);
}

static void lexer_scan_ws_comment(lexer_t *lex)
{
    while (lex->cur < lex->end) {
        switch (lex->cur[0]) {
        case '\r':
        case '\n':
            lex->col = 0;
            lex->line++;
            // fallthrough
        case '\f':
        case '\t':
        case '\v':
        case ' ':
            lexer_next_char(lex);
            break;
        case '/':
            if (lex->cur[1] == '/') {
                lexer_next_char(lex); lexer_next_char(lex);
                if (lex->cur[0] == '\n') {
                    lex->col = 0;
                    lex->line++;
                    lexer_next_char(lex);
                } else {
                    while (lex->cur < lex->end) {
                        lexer_next_char(lex);
                        if (lex->cur[0] == '\n') break;
                    }
                }
            } else if (lex->cur[1] == '*') {
                lexer_next_char(lex); lexer_next_char(lex);
                while (lex->cur < lex->end) {
                    if (lex->cur[0] == '\n') {
                        lex->col = 0;
                        lex->line++;
                    } else if (lex->cur[0] == '/') {
                        lexer_next_char(lex);
                        if (lex->cur[0] == '*') {
                            lex->token.line = lex->line;
                            lex->token.col = lex->col - 1;
                            lex->token.lexeme = lex->cur - 2;
                            lex->token.len = 2;
                            errf(lex, "nested /* ... */ comments are not allowed");
                        }
                        continue;
                    } else if (lex->cur[0] == '*') {
                        lexer_next_char(lex);
                        if (lex->cur[0] == '\n') {
                            lex->col = 0;
                            lex->line++;
                        } else if (lex->cur[0] == '/') {
                            lexer_next_char(lex);
                            break;
                        }
                    }
                    lexer_next_char(lex);
                }
            } else {
                return;
            }
            break;
        default:
            return;
        }
    }
}

static void check_reserved_type(token_t *tok, const char *rsv, token_type_t ty)
{
    if (memcmp(tok->lexeme, rsv, tok->len) == 0)
        tok->type = ty;
}

static void lexer_scan_symbol(lexer_t *lex)
{
    lex->token.lexeme = lex->cur;
    lex->token.line = lex->line;
    lex->token.col = lex->col;

    do {
        lexer_next_char(lex);
    } while (is_alnum(lex->cur[0]) || lex->cur[0] == '_');

    char ch = lex->token.lexeme[0];
    lex->token.len = lexer_count_length(lex);
    lex->token.type = tok_symbol;

    switch (lex->token.len) {
    case 8:
        switch (ch) {
        case 'c': // continue
            check_reserved_type(&lex->token, "continue", tok_continue);
            break;
        case 'r': // register
            check_reserved_type(&lex->token, "register", tok_register);
            break;
        case 'u': // unsigned
            check_reserved_type(&lex->token, "unsigned", tok_unsigned);
            break;
        case 'v': // volatile
            check_reserved_type(&lex->token, "volatile", tok_volatile);
            break;
        default:
            break;
        }
        break;
    case 7:
        switch (ch) {
        case 'd': // default
            check_reserved_type(&lex->token, "default", tok_default);
            break;
        case 't': // typedef
            check_reserved_type(&lex->token, "typedef", tok_typedef);
            break;
        default:
            break;
        }
        break;
    case 6:
        switch (ch) {
        case 'd': // double
            check_reserved_type(&lex->token, "double", tok_double);
            break;
        case 'e': // extern
            check_reserved_type(&lex->token, "extern", tok_extern);
            break;
        case 'r': // return
            check_reserved_type(&lex->token, "return", tok_return);
            break;
        case 's': // signed, sizeof, static, struct, switch
            ch = lex->token.lexeme[1];
            switch (ch) {
            case 'i':
                ch = lex->token.lexeme[2];
                switch (ch) {
                case 'g':
                    check_reserved_type(&lex->token, "signed", tok_signed);
                    break;
                case 'z':
                    check_reserved_type(&lex->token, "sizeof", tok_sizeof);
                    break;
                default:
                    break;
                }
                break;
            case 't':
                ch = lex->token.lexeme[2];
                switch (ch) {
                case 'a':
                    check_reserved_type(&lex->token, "static", tok_static);
                    break;
                case 'r':
                    check_reserved_type(&lex->token, "struct", tok_struct);
                    break;
                default:
                    break;
                }
                break;
            case 'w':
                check_reserved_type(&lex->token, "switch", tok_switch);
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
        break;
    case 5:
        switch (ch) {
        case 'b': // break
            check_reserved_type(&lex->token, "break", tok_break);
            break;
        case 'c': // const
            check_reserved_type(&lex->token, "const", tok_const);
            break;
        case 'f': // float
            check_reserved_type(&lex->token, "float", tok_float);
            break;
        case 's': // short
            check_reserved_type(&lex->token, "short", tok_short);
            break;
        case 'u': // union
            check_reserved_type(&lex->token, "union", tok_union);
            break;
        case 'w': // while
            check_reserved_type(&lex->token, "while", tok_while);
            break;
        default:
            break;
        }
        break;
    case 4:
        switch (ch) {
        case 'a': // auto
            check_reserved_type(&lex->token, "auto", tok_auto);
            break;
        case 'c': // case, char
            ch = lex->token.lexeme[1];
            switch (ch) {
            case 'a':
                check_reserved_type(&lex->token, "case", tok_case);
                break;
            case 'h':
                check_reserved_type(&lex->token, "char", tok_char);
                break;
            default:
                break;
            }
            break;
        case 'e': // else, enum
            ch = lex->token.lexeme[1];
            switch (ch) {
            case 'l':
                check_reserved_type(&lex->token, "else", tok_else);
                break;
            case 'n':
                check_reserved_type(&lex->token, "enum", tok_enum);
                break;
            default:
                break;
            }
            break;
        case 'g': // goto
            check_reserved_type(&lex->token, "goto", tok_goto);
            break;
        case 'l': // long
            check_reserved_type(&lex->token, "long", tok_long);
            break;
        case 'v': // void
            check_reserved_type(&lex->token, "void", tok_void);
            break;
        default:
            break;
        }
        break;
    case 3:
        switch (ch) {
        case 'f': // for
            check_reserved_type(&lex->token, "for", tok_for);
            break;
        case 'i': // int
            check_reserved_type(&lex->token, "int", tok_int);
            break;
        default:
            break;
        }
        break;
    case 2:
        switch (ch) {
        case 'd': // do
            check_reserved_type(&lex->token, "do", tok_do);
            break;
        case 'i': // if
            check_reserved_type(&lex->token, "if", tok_if);
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

static void lexer_scan_number(lexer_t *lex, bool is_float)
{
    int base = 10;

    lex->token.lexeme = lex->cur;
    lex->token.line = lex->line;
    lex->token.col = lex->col;

    strbuff_reset(&lex->sb);

    if (is_float)
        goto scan_float;

    // Check if there is other bases denoted by their prefix.
    //
    // This includes:
    //  - 0x  -- which means hexadecimal constant
    //  - 00  -- which means octal constant
    if (lex->cur[0] == '0') {
        lexer_next_char(lex);
        if (lex->cur[0] == 'x' || lex->cur[0] == 'X') {
            lexer_next_char(lex);
            while (is_xdigit(lex->cur[0])) {
                strbuff_append_char(&lex->sb, lex->cur[0]);
                lexer_next_char(lex);
            }
            if (lex->sb.n == 0)
                errf(lex, "missing digits after '0x' integer constant prefix");
            goto check_suffix;
        } else if (lex->cur[0] == '0') {
            do {
                strbuff_append_char(&lex->sb, lex->cur[0]);
                lexer_next_char(lex);
            } while (is_odigit(lex->cur[0]));
            base = 8;
            goto check_suffix;
        } else {
            strbuff_append_char(&lex->sb, '0');
        }
    } else if (lex->cur[0] == '.') {
        goto scan_float;
    } else { // Base-10 integer constant
        do {
            strbuff_append_char(&lex->sb, lex->cur[0]);
            lexer_next_char(lex);
        } while (is_digit(lex->cur[0]));
    }

    if (lex->cur[0] == '.') {
scan_float:
        do {
            strbuff_append_char(&lex->sb, lex->cur[0]);
            lexer_next_char(lex);
        } while (is_digit(lex->cur[0]));
        is_float = true;
    }

    if (lex->cur[0] == 'e' || lex->cur[0] == 'E') {
        strbuff_append_char(&lex->sb, lex->cur[0]);
        lexer_next_char(lex);
        if (lex->cur[0] == '+' || lex->cur[0] == '-') {
            strbuff_append_char(&lex->sb, lex->cur[0]);
            lexer_next_char(lex);
        }
        if (!is_digit(lex->cur[0]))
            errf(lex, "exponent has no digit");
        do {
            strbuff_append_char(&lex->sb, lex->cur[0]);
            lexer_next_char(lex);
        } while (is_digit(lex->cur[0]));
        is_float = true;
    }

check_suffix:
    switch (lex->cur[0]) {
    case 'f':
    case 'F':
        if (!is_float)
            errf(lex, "floating-point suffix '%c' on integer constant", lex->cur[0]);
        lexer_next_char(lex);
        break;
    case 'l':
        lexer_next_char(lex);
        if (is_float)
            break;
        if (lex->cur[0] == 'L') {
            errf(lex, "invalid suffix 'lL' on integer constant");
        } else if (lex->cur[0] == 'l') {
            lexer_next_char(lex);
            if (to_lower(lex->cur[0]) == 'u') // unsigned-long-long (llu or llU)
                lexer_next_char(lex);
            // else unsigned-long (lu or lU)
        } else if (to_lower(lex->cur[0]) == 'u') { // unsigned-long (lu or lU)
            lexer_next_char(lex);
        } // else long (l)
        break;
    case 'L':
        lexer_next_char(lex);
        if (is_float)
            break;
        if (lex->cur[0] == 'l') {
            errf(lex, "invalid suffix 'Ll' on integer constant");
        } else if (lex->cur[0] == 'L') {
            lexer_next_char(lex);
            if (to_lower(lex->cur[0]) == 'u') // unsigned-long-long (LLu or LLU)
                lexer_next_char(lex);
            // else unsigned-long (Lu or LU)
        } else if (to_lower(lex->cur[0]) == 'u') { // unsigned-long (Lu or LU)
            lexer_next_char(lex);
        } // else long (L)
        break;
    case 'u':
    case 'U':
        if (is_float)
            errf(lex, "integer suffix '%c' on floating-point constant", lex->cur[0]);
        lexer_next_char(lex);
        switch (lex->cur[0]) {
        case 'l':
            lexer_next_char(lex);
            if (lex->cur[0] == 'L')
                errf(lex, "invalid suffix 'ulL' on integer constant");
            else if (lex->cur[0] == 'l') // unsigned-long-long (ull or Ull)
                lexer_next_char(lex);
            // else unsigned-long (ul or Ul)
            break;
        case 'L':
            lexer_next_char(lex);
            if (lex->cur[0] == 'l')
                errf(lex, "invalid suffix 'uLl' on integer constant");
            else if (lex->cur[0] == 'L') // unsigned-long-long (uLL or ULL)
                lexer_next_char(lex);
            // else unsigned-long (uL or UL)
            break;
        default: // unsigned (u or U)
            break;
        }
        break;
    default:
        break;
    }

    lex->token.len = lexer_count_length(lex);
    lex->token.type = is_float ? tok_float_const : tok_int_const;
}

static int hex_char(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    else if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    else return ch - 'a' + 10;
}

static char scan_escape_sequence(lexer_t *lex)
{
    char ch = lex->cur[0];

    if (is_odigit(lex->cur[0])) { // \o
        ch = lex->cur[0] - '0';
        lexer_next_char(lex);
        if (is_odigit(lex->cur[0])) { // \oo
            ch = (ch << 3) + (lex->cur[0] - '0');
            lexer_next_char(lex);
            if (is_odigit(lex->cur[0])) { // \ooo
                ch = (ch << 3) + (lex->cur[0] - '0');
                lexer_next_char(lex);
                if (is_odigit(lex->cur[0]))
                    errf(lex, "octal escape sequence out of range");
            }
        }
    } else if (lex->cur[0] == 'x' || lex->cur[0] == 'X') {
        lexer_next_char(lex);
        if (!is_xdigit(lex->cur[0]))
            errf(lex, "no digit found after hexadecimal escape escape");
        ch = hex_char(lex->cur[0]);
        lexer_next_char(lex);
        if (is_xdigit(lex->cur[0])) {
            ch = (ch << 4) + hex_char(lex->cur[0]);
            lexer_next_char(lex);
            if (is_xdigit(lex->cur[0]))
                errf(lex, "hexadecimal escape sequence out of range");
        }
    } else {
        switch (lex->cur[0]) {
        case 'a':   ch = '\a'; break;
        case 'b':   ch = '\b'; break;
        case 'f':   ch = '\f'; break;
        case 'n':   ch = '\n'; break;
        case 'r':   ch = '\r'; break;
        case 't':   ch = '\t'; break;
        case 'v':   ch = '\v'; break;
        case '"':
        case '\\':
        case '\'':
            ch = lex->cur[0];
            break;
        default:
            errf(lex, "unknown escape sequence '\\%c'", lex->cur[0]);
            break;
        }

        lexer_next_char(lex);
    }

    return ch;
}

static void lexer_scan_string(lexer_t *lex)
{
    lex->token.line = lex->line;
    lex->token.col = lex->col;

    strbuff_reset(&lex->sb);
    lexer_next_char(lex);
    lex->token.lexeme = lex->cur;

    while (lex->cur[0] != '"') {
        if (lex->cur[0] == '\\') {
            lexer_next_char(lex);
            strbuff_append_char(&lex->sb, scan_escape_sequence(lex));
        } else if (lex->cur[0] == '\n') {
            errf(lex, "newline inside string literal should be escaped");
        } else if (lex->cur >= lex->end) {
            break;
        } else {
            strbuff_append_char(&lex->sb, lex->cur[0]);
            lexer_next_char(lex);
        }
    }

    if (lex->cur[0] != '"') {
        errf(lex, "unterminated string literal");
        exit(1);
    } else if (lex->sb.n > 509) {
        warnf(lex, "string literal with length '%d' exceed ISO C90 limit (509)", lex->sb.n);
    }

    lex->token.len = lexer_count_length(lex);
    lex->token.type = tok_string_lit;
    lexer_next_char(lex);
}

static void lexer_scan_char(lexer_t *lex)
{
    lex->token.line = lex->line;
    lex->token.col = lex->col;

    lexer_next_char(lex);

    if (lex->cur[0] == '\'') {
        errf(lex, "empty character constant");
        exit(1);
    }

    lex->token.lexeme = lex->cur;

    if (lex->cur[0] == '\\') {
        lexer_next_char(lex);
        scan_escape_sequence(lex);
    } else {
        lexer_next_char(lex);
    }

    if (lex->cur[0] != '\'') {
        errf(lex, "unterminated character constant");
        exit(1);
    }

    lex->token.len = lexer_count_length(lex);
    lex->token.type = tok_char_const;
    lexer_next_char(lex);
}

void lexer_scan(lexer_t *lex)
{
    lexer_scan_ws_comment(lex);

    if (lex->cur >= lex->end) {
        lex->token.type = tok_eof;
        lex->token.lexeme = lex->end;
        lex->token.len = 0;
        lex->token.line = lex->line;
        return;
    }

    lex->token.lexeme = lex->cur;
    lex->token.line = lex->line;
    lex->token.len = 1;
    lex->token.col = lex->col;

    if (is_alpha(lex->cur[0]) || lex->cur[0] == '_') {
        if (lex->cur[0] == 'L' && (lex->cur[1] == '"' || lex->cur[1] == '\''))
            errf(lex, "Wide character/string is not supported");
        else
            lexer_scan_symbol(lex);
        return;
    } else if (is_digit(lex->cur[0])) {
        lexer_scan_number(lex, /* is_float */ false);
        return;
    }

    switch (lex->cur[0]) {
    case '"':
        lexer_scan_string(lex);
        return;
    case '\'':
        lexer_scan_char(lex);
        return;
    case '.':
        lexer_next_char(lex);
        if (lex->cur[0] == '.' && lex->cur[1] == '.') {
            lex->token.type = tok_variadic;
            lex->token.len = 3;
            lexer_next_char(lex);
            lexer_next_char(lex);
        } else if (is_digit(lex->cur[0])) {
            lexer_putback_char(lex);
            lexer_scan_number(lex, /* is_float */ true);
        } else {
            lex->token.type = tok_dot;
        }
        return;
    case '#':
        lexer_next_char(lex);
        if (lex->cur[0] == '#') {
            lex->token.type = tok_pp_paste;
            lex->token.len = 2;
            lexer_next_char(lex);
        } else {
            lex->token.type = tok_hash;
        }
        return;
    case '+':
        lexer_next_char(lex);
        if (lex->cur[0] == '+') {
            lex->token.type = tok_inc;
            lex->token.len = 2;
            lexer_next_char(lex);
        } else if (lex->cur[0] == '=') {
            lex->token.type = tok_assign_add;
            lex->token.len = 2;
            lexer_next_char(lex);
        } else {
            lex->token.type = tok_plus;
        }
        return;
    case '-':
        lexer_next_char(lex);
        if (lex->cur[0] == '-') {
            lex->token.type = tok_dec;
            lex->token.len = 2;
            lexer_next_char(lex);
        } else if (lex->cur[0] == '>') {
            lex->token.type = tok_arrow;
            lex->token.len = 2;
            lexer_next_char(lex);
        } else if (lex->cur[0] == '=') {
            lex->token.type = tok_assign_sub;
            lex->token.len = 2;
            lexer_next_char(lex);
        } else {
            lex->token.type = tok_minus;
        }
        return;
    case '*':
        lexer_next_char(lex);
        if (lex->cur[0] == '=') {
            lex->token.type = tok_assign_mul;
            lex->token.len = 2;
            lexer_next_char(lex);
        } else {
            lex->token.type = tok_star;
        }
        return;
    case '/':
        lexer_next_char(lex);
        if (lex->cur[0] == '=') {
            lex->token.type = tok_assign_div;
            lex->token.len = 2;
            lexer_next_char(lex);
        } else {
            lex->token.type = tok_slash;
        }
        return;
    case '%':
        lexer_next_char(lex);
        if (lex->cur[0] == '=') {
            lex->token.type = tok_assign_mod;
            lex->token.len = 2;
            lexer_next_char(lex);
        } else {
            lex->token.type = tok_percent;
        }
        return;
    case '<':
        lexer_next_char(lex);
        if (lex->cur[0] == '=') {
            lex->token.type = tok_cmple;
            lex->token.len = 2;
            lexer_next_char(lex);
        } else if (lex->cur[0] == '<') {
            if (lex->cur[1] == '=') {
                lex->token.type = tok_assign_lshift;
                lex->token.len = 3;
                lexer_next_char(lex);
            } else {
                lex->token.type = tok_lshift;
                lex->token.len = 2;
            }
            lexer_next_char(lex);
        } else {
            lex->token.type = tok_langle;
        }
        return;
    case '>':
        lexer_next_char(lex);
        if (lex->cur[0] == '=') {
            lex->token.type = tok_cmpge;
            lex->token.len = 2;
            lexer_next_char(lex);
        } else if (lex->cur[0] == '>') {
            if (lex->cur[1] == '=') {
                lex->token.type = tok_assign_rshift;
                lex->token.len = 3;
                lexer_next_char(lex);
            } else {
                lex->token.type = tok_rshift;
                lex->token.len = 2;
            }
            lexer_next_char(lex);
        } else {
            lex->token.type = tok_rangle;
        }
        return;
    case '&':
        lexer_next_char(lex);
        if (lex->cur[0] == '=') {
            lex->token.type = tok_assign_band;
            lex->token.len = 2;
            lexer_next_char(lex);
        } else if (lex->cur[0] == '&') {
            lex->token.type = tok_logand;
            lex->token.len = 2;
            lexer_next_char(lex);
        } else {
            lex->token.type = tok_ampersand;
        }
        return;
    case '^':
        lexer_next_char(lex);
        if (lex->cur[0] == '=') {
            lex->token.type = tok_assign_bxor;
            lex->token.len = 2;
            lexer_next_char(lex);
        } else {
            lex->token.type = tok_caret;
        }
        break;
    case '|':
        lexer_next_char(lex);
        if (lex->cur[0] == '=') {
            lex->token.type = tok_assign_bor;
            lex->token.len = 2;
            lexer_next_char(lex);
        } else if (lex->cur[0] == '|') {
            lex->token.type = tok_logor;
            lex->token.len = 2;
            lexer_next_char(lex);
        } else {
            lex->token.type = tok_pipe;
        }
        return;
    case '!':
        lexer_next_char(lex);
        if (lex->cur[0] == '=') {
            lex->token.type = tok_cmpne;
            lex->token.len = 2;
            lexer_next_char(lex);
        } else {
            lex->token.type = tok_bang;
        }
        return;
    case '=':
        lexer_next_char(lex);
        if (lex->cur[0] == '=') {
            lex->token.type = tok_cmpeq;
            lex->token.len = 2;
            lexer_next_char(lex);
        } else {
            lex->token.type = tok_equal;
        }
        return;
    case '~':
        lex->token.type = tok_tilde;
        break;
    case '?':
        lex->token.type = tok_question;
        break;
    case ':':
        lex->token.type = tok_colon;
        break;
    case ';':
        lex->token.type = tok_semicolon;
        break;
    case '{':
        lex->token.type = tok_lbrace;
        break;
    case '}':
        lex->token.type = tok_lbrace;
        break;
    case '(':
        lex->token.type = tok_lparen;
        break;
    case ')':
        lex->token.type = tok_rparen;
        break;
    case '[':
        lex->token.type = tok_lsquare;
        break;
    case ']':
        lex->token.type = tok_rsquare;
        break;
    case ',':
        lex->token.type = tok_comma;
        break;
    default:
        lex->token.type = tok_invalid;
        break;
    }

    lexer_next_char(lex);
}

void lexer_setup(lexer_t *lex, file_t *file)
{
    memset(&lex->token, 0, sizeof(lex->token));
    strbuff_setup(&lex->sb);

    lex->line = 1;
    lex->col = 1;
    lex->token.type = tok_invalid;
    lex->file = file;
    lex->cur = (const char *)file->content;
    lex->end = lex->cur + file->size;
}

void lexer_cleanup(lexer_t *lex)
{
    strbuff_cleanup(&lex->sb);
    memset(lex, 0, sizeof(*lex));
}

// === Main ==================================================================
// ===========================================================================

void print_token(file_t *file)
{
    lexer_t lex;
    lexer_setup(&lex, file);

    do {
        lexer_scan(&lex);
#if 1
        int l = lex.token.len;
        int ln = lex.token.line;
        int co = lex.token.col;
        const char *f = file->path;
        const char *s = lex.token.lexeme;
        const char *ty = token_type_to_str(&lex.token);
        msgf(" [%s] %d:%-4d | %s   “%.*s” (len=%d)", f, ln, co, ty, l, s, l);
#endif
    } while (lex.token.type != tok_eof);

    lexer_cleanup(&lex);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s file.c\n", *argv);
        return 1;
    }

    file_t file;
    if (file_read(&file, argv[1]) == -1)
        return 1;

    print_token(&file);
    file_close(&file);
}
