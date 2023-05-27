#ifndef NKCC_TOKEN_H
#define NKCC_TOKEN_H

enum __nkcc_token_type {
#define __TOKEN_DEF(__tok_type, __) tok_##__tok_type,
    #include "token.def"
#undef __TOKEN_DEF
};

typedef enum __nkcc_token_type token_type_t;

struct __nkcc_token {
    token_type_t type;
    const char *lexeme;
    int len, col, line;
};

typedef struct __nkcc_token token_t;

static const char *token_type_tables[] = {
#define __TOKEN_DEF(__, __tok_name) __tok_name,
    #include "token.def"
#undef __TOKEN_DEF
    NULL
};

static const char *token_type_to_str(token_t *tok)
{
    if (tok->type >= tok_eof && tok->type < tok_invalid)
        return token_type_tables[tok->type];

    return "<INVALID>";
}


#endif
