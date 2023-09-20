/*
 * declarator_specifier() returns 'struct typetree'
 * declarator() returns 'struct declarator'
 * initializer() returns 'struct inittree'
 * declaration() returns 'struct decltree'
 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

enum typetree_kind {
    TY_VOID,
    TY_CHAR,
    TY_BOOL,
    TY_INT,
    TY_SHORT,
    TY_LONG,
    TY_FLOAT,
    TY_DOUBLE,
    TY_LDOUBLE,
    TY_ARRAY,
    TY_POINTER,
    TY_ENUM,
    TY_STRUCT,
    TY_UNION,
    TY_TYPEDEF,
    TY_FUNCTION
};

enum linkage_kind {
    LINK_NONE,
    LINK_EXTERN,
    LINK_INTERNAL
};

enum storage_class_kind {
    ST_EXTERN,
    ST_STATIC,
    ST_REGISTER,
    ST_AUTO,
    ST_TYPEDEF
};

enum qualifier_kind {
    QL_CONST    = 1,
    QL_VOLATILE = 2,
    QL_CV       = QL_CONST | QL_VOLATILE, /* 3 */
    QL_RESTRICT = 4,
    QL_CR       = QL_CONST | QL_RESTRICT, /* 5 */
    QL_VR       = QL_VOLATILE | QL_RESTRICT, /* 6 */
    QL_CVR      = QL_CONST | QL_VOLATILE | QL_RESTRICT /* 7 */
};

typedef char *string_ref[2];
typedef struct typetree_t typetree_t;
typedef struct enumdecl_t enumdecl_t;
typedef struct struct_union_t struct_union_t;

struct enumdecl_t {
    enumdecl_t *next;
    string_ref name;
    uint64_t value;
};

struct struct_union_t {
    struct_union_t *next;
    declaration_t *ty; /* ty must not have initializer */
    unsigned int offset;
    int bitfields; /* -1 if this member doesn't define bitfields */
};

struct function_type {
    typetree_t **param;
    unsigned int nparam;
};

struct typetree_t {
    typetree_kind kind;
    storage_class_kind storage_class;
    qualifier_kind qualifier;
    unsigned int size; /* if kind == TY_ARRAY this is the number of elements */
    unsigned int align;
    bool is_unsigned;
    bool is_complete;
    union {
        typetree_t *type_def; /* FIXME: this should be declarator */
        typetree_t *array; /* FIXME: what's the correct repr? */
        struct {
            string_ref name;
            union {
                enumdecl_t *e;
                struct_union_t *s;
                struct_union_t *u;
            } node;
        } tag;
        struct {
            typetree_t *base;
            qualifier_kind qualifier;
        } pointer;
    } ty;
};

struct declarator;
struct inittree;
struct decltree;
