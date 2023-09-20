#ifndef CPP_H
#define CPP_H

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* flags for cond_stack */
#define CPP_COND_ELSIF      1 /* this branch has #elif/#else */
/* limits for cond_stack */
#define CPP_COND_MAX       32 /* nested, per file */

/* flags for cpp_macro */
#define CPP_MACRO_FUNC      1
#define CPP_MACRO_VA_ARG    2
/* limits for cpp_macro */
#define CPP_MACRO_MAX       16384 /* per translation unit */


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
    TK_eom, /* used to indicate the end of rescanning phase of a macro */
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
    ushort id;
    uint size;
    const char *name;
    const char *path;
    const char *dirpath;
    uchar *data;
} cpp_file;

typedef struct {
    uchar kind;
    uchar flags;
    ushort wscount;
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
    enum { COND_IF, COND_ELIF, COND_ELSE, COND_DEAD } ctx;
    cpp_token token; /* for header guard detection or diagnostic */
    struct cond_stack *prev;
} cond_stack;

typedef struct macro_stack {
    string_ref name;
    ht_t *arg;
    cpp_token_array tok; /* used during substitution */
    const cpp_token *p; /* substituted, used during rescanning from `tok` */
    struct macro_stack *prev; /* nested */
} macro_stack;

typedef struct cpp_buffer {
    uchar *data;
    uint len;
    uint cap;
    struct cpp_buffer *prev;
} cpp_buffer;

typedef struct cpp_stream {
    uchar flags;
    uint lineno;
    uint pplineno;
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

typedef struct {
    cpp_token_array ts;
    cpp_token_array temp;
    cpp_stream *stream;
    macro_stack *file_macro;
    arg_stream *argstream;
    ht_t macro; /* key=string_ref, val=cpp_macro* */
    ht_t cached_file; /* key=cpp_file::path, val=cpp_file* */
    cpp_buffer *buf;
    const uchar *ppdate;
    const uchar *pptime;
    uint ppcounter;
    /* add more... */
} cpp_context;


/* ---- function declarations ---------------------------------------------- */

/* cpp.c */
void cpp_context_setup(cpp_context *ctx);
void cpp_context_cleanup(cpp_context *ctx);
void cpp_run(cpp_context *ctx, cpp_file *file);
void cpp_print(cpp_context *ctx, cpp_file *file, FILE *fp);
const uchar *cpp_buffer_append(cpp_context *ctx, const uchar *p, uint psize);
const uchar *cpp_buffer_append_ch(cpp_context *ctx, uchar ch);

/* file.c */
void cpp_file_setup(void);
void cpp_file_cleanup(void);
void cpp_file_add_sysdir(const char *name);
cpp_file *cpp_file_open(const char *path, const char *cwd);
void cpp_file_close(cpp_file *file);
cpp_file *cpp_file_id(ushort id);

/* lex.c */
void cpp_lex_string(cpp_stream *s, cpp_token *tk, tkchar q);
void cpp_lex_scan(cpp_stream *s, cpp_token *tk);

/* token.c */
const char *cpp_token_kind(uchar kind);
uint cpp_token_splice(cpp_token *tk, uchar *buf, uint bufsz);
void cpp_token_print(FILE *fp, cpp_token *tk);
void cpp_token_unpp(cpp_token *tk);
void cpp_token_array_setup(cpp_token_array *ts, uint max);
void cpp_token_array_clear(cpp_token_array *ts);
void cpp_token_array_append(cpp_token_array *ts, cpp_token *tk);
void cpp_token_array_move(cpp_token_array *dts, cpp_token_array *sts);
void cpp_token_array_cleanup(cpp_token_array *ts);

#endif
