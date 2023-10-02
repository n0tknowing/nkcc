#ifndef CPP_H
#define CPP_H

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "string_pool.h"
#include "hash_table.h"

/* ---- helper macros ------------------------------------------------------ */

#define ALIGN(x, y)    ((x) + ((y) - 1)) & (~((y) - 1))
#define MIN(x, y)      ((x) < (y) ? (x) : (y))
#define MAX(x, y)      ((x) > (y) ? (x) : (y))
#define HAS_FLAG(x, y) (((x) & (y)) == (y))
#define AT_BOL(_t)     (HAS_FLAG((_t)->flags, CPP_TOKEN_BOL))
#define PREV_SPACE(_t) (HAS_FLAG((_t)->flags, CPP_TOKEN_SPACE))
#define LITREF(x)      string_ref_newlen((x), sizeof((x)) - 1)

#if defined(__GNUC__) || defined(__clang__)
#define likely(x)      (__builtin_expect(!!(x), 1))
#define unlikely(x)    (__builtin_expect(!!(x), 0))
#else
#define likely(x)      x
#define unlikely(x)    x
#endif


/* ---- typedefs ----------------------------------------------------------- */

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned int tkchar;


/* ---- flags and limits --------------------------------------------------- */

/* flags for cpp_file */
#define CPP_FILE_NONL        1 /* no newline at end of file */
#define CPP_FILE_FREED       2 /* file has been freed */
/* limits for cpp_file */
#define CPP_FILE_MAX_USED    1024 /* it's still too big */
#define CPP_FILE_MAX_SIZE    (1U << 31) /* 2GiB */

/* flags for cpp_token */
#define CPP_TOKEN_BOF       1 /* token is at beginning of file */
#define CPP_TOKEN_BOL       2 /* token is at beginning of line */
#define CPP_TOKEN_NOEXPAND  4 /* token is macro and cannot be expanded again */
#define CPP_TOKEN_ESCNL     8 /* there is "\\\n" in the token */
#define CPP_TOKEN_FLNUM    16 /* token is floating constant */
#define CPP_TOKEN_SPACE    32 /* token is followed by whitespace */

/* flags for cond_stack */
#define CPP_COND_ELSIF      1 /* this branch has #elif/#else */
#define CPP_COND_SKIP       2 /* we are looking for #elif/#else/#endif */
#define CPP_COND_GUARD      4 /* #ifdef ... #define was checked */
/* limits for cond_stack */
#define CPP_COND_MAX       32 /* nested, per file */

/* flags for cpp_macro */
#define CPP_MACRO_FUNC      1 /* this macro is function-like */
#define CPP_MACRO_BUILTIN   2 /* this macro is builtin macros */
#define CPP_MACRO_VA_ARG    4 /* this macro arg is variadic args */
#define CPP_MACRO_GUARD     8 /* this macro is used as header guard */
#define CPP_MACRO_EXPR     16 /* this macro is used in #if/#elif expression */
/* limits for cpp_macro */
#define CPP_MACRO_MAX       16384 /* per translation unit */

/* limits for cpp_buffer */
#define CPP_BUFFER_CAPA     (1U << 24) /* 16MiB */


/* ---- enums -------------------------------------------------------------- */

enum _cpp_token_kind {
    /* C language tokens (some are used in the preprocessor) */
    TK_integer_const = 128,
    TK_float_const,
    TK_char_const,
    TK_string,
    TK_identifier,

    TK_elipsis, // ...
    TK_lshift, // <<
    TK_rshift, // >>
    TK_incr, // ++
    TK_decr, // --
    TK_arrow, // ->
    TK_and, // &&
    TK_or, // ||
    TK_eq, // ==
    TK_ne, // !=
    TK_le, // <=
    TK_ge, // >=
    TK_asg_add, // +=
    TK_asg_sub, // -=
    TK_asg_mul, // *=
    TK_asg_div, // /=
    TK_asg_mod, // %=
    TK_asg_lshift, // <<=
    TK_asg_rshift, // >>=
    TK_asg_band, // &=
    TK_asg_bxor, // ^=
    TK_asg_bor, // |=

    TK_continue,
    TK_register,
    TK_unsigned,
    TK_volatile,
    TK_default,
    TK_typedef,
    TK_double,
    TK_extern,
    TK_return,
    TK_signed,
    TK_sizeof,
    TK_static,
    TK_struct,
    TK_switch,
    TK_break,
    TK_const,
    TK_float,
    TK_short,
    TK_union,
    TK_while,
    TK_auto,
    TK_case,
    TK_char,
    TK_else,
    TK_enum,
    TK_goto,
    TK_long,
    TK_void,
    TK_for,
    TK_int,
    TK_do,
    TK_if,

    /* special tokens */
    TK_paste,
    TK_number,
    TK_eom, /* used to indicate the end of a macro replacement list and
               rescanning phase */
    TK_eof = 255
};

typedef enum {
    CPP_DIR_IF = 0,
    CPP_DIR_IFDEF,
    CPP_DIR_IFNDEF,
    CPP_DIR_ELIF,
    CPP_DIR_ELSE,
    CPP_DIR_ENDIF,
    CPP_DIR_INCLUDE,
    CPP_DIR_DEFINE,
    CPP_DIR_UNDEF,
    CPP_DIR_LINE,
    CPP_DIR_PRAGMA,
    CPP_DIR_ERROR,
    CPP_DIR_UNKNOWN = 32
} cpp_directive_kind;


/* ---- structs and unions ------------------------------------------------- */

typedef struct {
    uchar flags;
    ushort no;
    uint size;
    uint inode, devid;
    string_ref name;
    string_ref path;
    string_ref dirpath;
    uchar *data;
} cpp_file;

typedef struct {
    uchar kind;
    ushort flags;
    ushort fileno;
    uint lineno;
    uint length;
    const uchar *p;
} cpp_token;

typedef struct {
    uint n;
    uint max;
    cpp_token *tokens;
} cpp_token_array;

typedef struct {
    uchar flags;
    ushort fileno;
    uint n_param;
    string_ref name;
    string_ref *param;
    cpp_token_array body;
} cpp_macro;

typedef struct {
    uchar flags;
    string_ref param;
    cpp_token_array body;
} cpp_macro_arg;

typedef struct cond_stack {
    uchar flags;
    string_ref guard_name; /* non-zero if CPP_COND_GUARD flag is set */
    enum {
        COND_IF, COND_IFDEF, COND_IFNDEF,
        COND_ELIF,
        COND_ELSE,
        COND_SKIP
    } ctx;
    cpp_token token; /* for diagnostic */
    struct cond_stack *prev; /* nested */
} cond_stack;

typedef struct macro_stack {
    string_ref name;
    cpp_token_array tok; /* used during substitution */
    const cpp_token *p; /* substituted, used during rescanning from `tok` */
    struct macro_stack *prev; /* nested */
} macro_stack;

typedef struct cpp_buffer {
    uchar *data;
    uint len;
} cpp_buffer;

typedef struct cpp_stream {
    uchar flags;
    uint lineno;
    uint pplineno_loc;
    uint pplineno_val;
    const char *fname;
    const char *ppfname;
    const uchar *p;
    cpp_file *file;
    cond_stack *cond;
    struct cpp_stream *prev; /* #include may modify this */
} cpp_stream;

typedef struct arg_stream {
    const cpp_token *p; /* the tokens in an argument from cpp_macro_arg */
    macro_stack *macro;
    struct arg_stream *prev;
} arg_stream;

/*
 * `ts` is the token array after preprocessing a file, used by later phases.
 * `temp` is token array for backtrack.
 * `line` is token array for expanding macros in #if/#elif/#line/#include.
 *        unlike macro expansion in `file_macro` and `argstream`, it's
 *        recycled.
 * `stream` is the file stream that's being preprocessed.
 * `file_macro` is where all macros expanded in a translation unit.
 * `argstream` is a fake stream that's used when expanding a macro argument.
 * `macro` is where all macros in a translation unit defined.
 * `cached_file` is used to store cpp_file that's not guarded either by header
 *               guard or #pragma once, so we can avoid reading the same file.
 *               a guarded file will never be cached.
 * `buf` is dynamic buffer, used to store temporary token pointer.
 * `ppdate` is the cached value of __DATE__ macro.
 * `pptime` is the cached value of __TIME__ macro.
 */
typedef struct {
    uchar flags;
    cpp_token_array ts;
    cpp_token_array temp;
    cpp_token_array line;
    cpp_stream *stream;
    macro_stack *file_macro;
    arg_stream *argstream;
    ht_t macro;
    ht_t cached_file;
    ht_t guarded_file;
    cpp_buffer buf;
    const uchar *ppdate;
    const uchar *pptime;
    /* add more... */
} cpp_context;


/* ---- function declarations ---------------------------------------------- */

/* cpp.c */
void cpp_context_setup(cpp_context *ctx);
void cpp_context_cleanup(cpp_context *ctx);
void cpp_run(cpp_context *ctx, cpp_file *file);
void cpp_print(cpp_context *ctx, cpp_file *file, FILE *fp);
void cpp_macro_define(cpp_context *ctx, const char *in);
void cpp_macro_undefine(cpp_context *ctx, const char *in);
void cpp_search_path_append(cpp_context *ctx, const char *dirpath);
const uchar *cpp_buffer_append(cpp_context *ctx, const uchar *p, uint psize);
const uchar *cpp_buffer_append_ch(cpp_context *ctx, uchar ch);

/* file.c */
void cpp_file_setup(void);
void cpp_file_cleanup(void);
cpp_file *cpp_file_open(const char *path, const char *name);
cpp_file *cpp_file_open2(string_ref path, string_ref name, struct stat *sb);
void cpp_file_close(cpp_file *file);
cpp_file *cpp_file_no(ushort no);

/* lex.c */
void cpp_lex_setup(cpp_context *ctx);
void cpp_lex_cleanup(void);
void cpp_lex_string(cpp_stream *s, cpp_token *tk, tkchar q);
void cpp_lex_scan(cpp_stream *s, cpp_token *tk);

/* token.c */
const char *cpp_token_kind(uchar kind);
uint cpp_token_splice(const cpp_token *tk, uchar *buf, uint bufsz);
void cpp_token_print(FILE *fp, const cpp_token *tk);
void cpp_token_unpp(const cpp_token *tk);
string_ref cpp_token_intern_id(const cpp_token *tk);
void cpp_token_array_setup(cpp_token_array *ts, uint max);
void cpp_token_array_clear(cpp_token_array *ts);
void cpp_token_array_append(cpp_token_array *ts, const cpp_token *tk);
void cpp_token_array_move(cpp_token_array *dts, cpp_token_array *sts);
void cpp_token_array_cleanup(cpp_token_array *ts);

#endif
