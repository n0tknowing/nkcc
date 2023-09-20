#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct declaration {
    void *dummy;
};

struct expression {
    void *dummy;
};

enum statement_kind {
    ST_LABEL,
    ST_CASE,
    ST_DEFAULT,
    ST_BLOCK,
    ST_EXPR,
    ST_IF,
    ST_SWITCH,
    ST_WHILE,
    ST_DO,
    ST_FOR,
    ST_GOTO,
    ST_CONTINUE,
    ST_BREAK,
    ST_RETURN
};

enum statement_flag {
    SFLG_BLOCK_IS_DECL = 1, /* if not set then block is statement */
    SFLG_FOR_CLAUSE_IS_DECL = 2, /* if not set then clause1 is expression */
};

struct statement {
    enum statement_kind kind;
    enum statement_flag flag;
    union {
        struct {
            char *name; /* label name */
            struct statement *body;
        } label_s;
        struct { /* TODO: remove this */
            int64_t cexp; /* controlling expression */
            struct statement *body;
        } case_s;
        struct { /* TODO: is this correct? any better representation? */
            union {
                struct declaration *decl;
                struct statement *stmt;
            } body;
            struct statement *next;
        } block_s;
        struct {
            struct expression *cexp; /* controlling expression */
            struct statement *body; /* body if true */
            struct statement *or_else; /* body if false */
        } if_s;
        struct { /* TODO: body should be array or list */
            struct expression *cexp; /* controlling expression */
            struct statement *body;
            struct statement *defa; /* default label */
        } switch_s;
        struct {
            struct expression *cexp;
            struct statement *body;
        } while_s;
        struct {
            struct statement *body;
            struct expression *cexp;
        } do_s;
        struct {
            union {
                struct declaration *decl;
                struct expression *expr;
            } clause1;
            struct expression *cexp;
            struct expression *expr;
            struct statement *body;
        } for_s;
        struct { char *label; } goto_s;
        struct { struct expression *expr; } return_s;
        struct { struct expression *body; } expr_s;
    };
};

static struct statement *new_stmt(enum statement_kind kind)
{
    struct statement *s = calloc(1, sizeof(*s));
    assert(s != NULL);
    s->kind = kind;
    return s;
}

struct statement *label_s(char *name, struct statement *body)
{
    struct statement *s = new_stmt(ST_LABEL);
    s->label_s.name = name;
    s->label_s.body = body;
    return s;
}

struct statement *goto_s(char *label_name)
{
    struct statement *s = new_stmt(ST_GOTO);
    s->goto_s.label = label_name;
    return s;
}

struct statement *return_s(struct expression *expr)
{
    struct statement *s = new_stmt(ST_RETURN);
    s->return_s.expr = expr;
    return s;
}

struct statement *expr_s(struct expression *expr)
{
    struct statement *s = new_stmt(ST_EXPR);
    s->expr_s.body = expr;
    return s;
}

struct statement *if_s(struct expression *cexp,
                       struct statement *body, struct statement *or_else)
{
    struct statement *s = new_stmt(ST_IF);
    s->if_s.cexp = cexp;
    s->if_s.body = body;
    s->if_s.or_else = or_else;
    return s;
}

struct statement *while_s(struct expression *cexp, struct statement *body)
{
    struct statement *s = new_stmt(ST_WHILE);
    s->while_s.cexp = cexp;
    s->while_s.body = body;
    return s;
}

struct statement *do_s(struct expression *cexp, struct statement *body)
{
    struct statement *s = new_stmt(ST_DO);
    s->do_s.cexp = cexp;
    s->do_s.body = body;
    return s;
}

struct statement *for_s(struct expression *expr1, struct declaration *decl,
                        struct expression *cexpr, struct expression *expr3,
                        struct statement *body)
{
    struct statement *s = new_stmt(ST_FOR);
    s->for_s.cexp = cexpr;
    s->for_s.expr = expr3;
    s->for_s.body = body;

    if (expr1 == NULL) {
        s->flag = SFLG_FOR_CLAUSE_IS_DECL;
        s->for_s.clause1.decl = decl;
    } else {
        s->for_s.clause1.expr = expr1;
    }

    return s;
}

void statement_free(struct statement *s)
{
    if (s == NULL)
        return;

    switch (s->kind) {
    case ST_LABEL:
        statement_free(s->label_s.body);
        s->label_s.name = NULL;
        break;
    case ST_GOTO:
        s->goto_s.label = NULL;
        break;
    case ST_RETURN:
        // expression_free(s->return_s.expr);
        s->return_s.expr = NULL;
        break;
    case ST_EXPR:
        // expression_free(s->expr_s.body);
        s->expr_s.body = NULL;
        break;
    case ST_IF:
        statement_free(s->if_s.body);
        statement_free(s->if_s.or_else);
        // expression_free(s->if_s.cexp);
        s->if_s.cexp = NULL;
        break;
    case ST_WHILE:
        statement_free(s->while_s.body);
        // expression_free(s->while_s.cexp);
        s->while_s.cexp = NULL;
        break;
    case ST_DO:
        statement_free(s->do_s.body);
        // expression_free(s->do_s.cexp);
        s->do_s.cexp = NULL;
        break;
    case ST_FOR:
        statement_free(s->for_s.body);
        // expression_free(s->for_s.expr);
        s->for_s.expr = NULL;
        // expression_free(s->for_s.cexp);
        s->for_s.cexp = NULL;
        if (s->flag == SFLG_FOR_CLAUSE_IS_DECL) {
            // declaration_free(s->for_s.clause1.decl);
            s->for_s.clause1.decl = NULL;
        } else {
            // expression_free(s->for_s.clause1.expr);
            s->for_s.clause1.expr = NULL;
        }
        break;
    default:
        return;
    }

    free(s);
}

int main(void)
{
    // for (;;);

    assert(sizeof(struct statement) == 40);
    struct statement *body = expr_s(NULL);
    struct statement *loop = for_s(NULL, NULL, NULL, NULL, body);
    statement_free(loop);
}
