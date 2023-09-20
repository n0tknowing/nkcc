typedef struct enum_node_t {
    struct enum_node_t *next;
    string_ref name;
    uint64_t value;
} enum_node_t;

typedef struct enum_t {
    enum_node_t *enums;
    string_ref tag;
};

static enum_node_t *enumerator_list(void)
{
    enum_node_t *head = NULL, *tail = NULL;
    uint64_t value = 0;
    
    next_token();

    while (token != '}') {
        enum_node_t *node;
        string_ref name;

        if (token == TK_eof)
            error("unexpected end of file");
        else if (token != TK_Identifier)
            error("expect identifier");

        name = get_token_string();
        if (symtab_lookup(&g_ctx.symtab.ident, name))
            error("%s was previously defined", name);
        symtab_insert(&g_ctx.symtab.ident, name, NULL);

        node = arena_alloc(&g_ctx.a.tags, sizeof(enum_node_t));
        node->next = NULL;
        node->name = name;

        next_token();

        if (token == '=')
            value = constant_expression();
        node->value = value++;

        if (head == NULL) head = node;
        else tail->next = node;
        tail = node;

        if (token == ',') {
            next_token();
            if (token == '}')
                break;
        } else {
            break;
        }
    }

    expect_token('}');
    return head;
};
