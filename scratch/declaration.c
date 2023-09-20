// === Declarator ============================================================
// ===========================================================================

void pointer(struct declaration *decl)
{
    while (token == '*') {
        decl->pointer = decl->pointer + 1;
        next_token();
        switch (token) {
        case T_CONST:
            if (decl->is_const)
                warn("duplicate 'const' type qualifier");
            decl->is_const = true;
            next_token();
            break;
        case T_VOLATILE:
            if (decl->is_volatile)
                warn("duplicate 'volatile' type qualifier");
            decl->is_volatile = true;
            next_token();
            break;
        }
    }
}

struct declaration *declarator(struct decltype *type)
{
    struct declaration *decl = new_declaration();
    decl->type = type;
    pointer(decl);

    while (1) {
        if (token == '(') {
            decl = declarator(type);
            expect_token(')');
        } else if (token == T_IDENTIFIER) {
            decl->name = token_string;
            next_token();
            if (token == '[') {
                next_token();
                if (token != ']') {
                    decl->array_sz = constant_expression();
                    if (decl->array_size < 0)
                        error("array size must greater than zero");
                }
                expect_token(']');
                decl->is_array = true;
            } else if (token == '(') {
                next_token();
                if (token != ')')
                    function_declarator(decl);
                expect_token(')');
                decl->is_function = true;
            }
        } else {
            break;
        }
    }

    return decl;
}

// === Declaration ===========================================================
// ===========================================================================

void enumerator_list(struct enumlist *enumlist)
{
    next_token();

    value = 0
    while (token != '}') {
        if (token == T_EOF)
            error("unexpected end of file");
        else if (token != T_IDENTIFIER)
            error("expected identifier while parsing enum declaration");
        if (lookup_symtab(current_scope, token_string))
            error("identifier was previously defined");
        member_name = token_string
        insert_symtab(current_scope, member_name, NULL);
        next_token();
        if (token == '=')
            value = constant_expression();
        insert_enum_member(enumlist, member_name, value);
        value = value + 1;
        if (token == ',') {
            next_token();
            if (token == '}')
                break;
        } else {
            break;
        }
    }

    if (size_enum_member(enum_list) == 0)
        error("enum has no member");
}

void enum_specifier(struct decltype *type)
{
    next_token();

    if (token == T_IDENTIFIER) {
        if (lookup_tag(tagtab, token_string))
            error("tag was previously defined");
        type->enumlist.tag = token_string;
        insert_tag(tagtab, token_string);
        next_token();
    }

    if (token == '{') {
        if (type->enumlist.tag == NULL)
            type->enumlist.tag = "anon_enum"; // randomize
        enumerator_list(type->enumlist);
        expect_token('}');
    }

    type->base = ENUM;
}

void struct_declaration_list(struct struct_or_union *struct_or_union)
{
    next_token();
    i = 0;

    while (token != '}') {
        if (token == T_EOF)
            error("unexpected end of file");
        struct decltype *type = declaration_specifier();
        if (type->storage_class)
            error("storage class is not allowed on struct/union member");
        do {
            struct declaration *decl = declarator(type);
            if (token == ':')
                struct_or_union->members[i].bitfield = constant_expression();
            insert_struct_or_union_member(struct_or_union, decl);
            i = i + 1;
        } while (token == ',');
        expect_token(';');
    }

    if (size_struct_or_union(struct_or_union) == 0)
        error("struct/union has no member");
}

void struct_or_union_specifier(struct delctype *type)
{
    next_token();

    if (token == T_IDENTIFIER) {
        if (lookup_tag(tagtab, token_string))
            error("tag was previously defined");
        insert_tag(tagtab, token_string);
        type->struct_or_union.tag = token_string;
        next_token();
    }

    if (token == '{') {
        if (type->struct_or_union.tag == NULL)
            type->struct_or_union.tag = "anon_struct_or_union"; // randomize
        struct_declaration_list(type->struct_or_union);
        expect_token('}');
    }

    type->base = STRUCT;
}

void storage_class_specifier(struct decltype *type)
{
    switch (token) {
    case T_TYPEDEF:
    case T_EXTERN:
    case T_STATIC:
    case T_AUTO:
    case T_REGISTER:
        if (type->storage_class)
            error("multiple storage class specifier is not allowed");
        if (type->base)
            warn("storage class is not at beginning of declaration");
        type->storage_class = token;
        next_token();
        break;
    default:
        break;
    }
}

void type_qualifier(struct decltype *type)
{
    switch (token) {
    case T_CONST:
        if (type->is_const)
            warn("duplicate 'const' type qualifier");
        type->is_const = true;
        next_token();
        break;
    case T_VOLATILE:
        if (type->is_volatile)
            warn("duplicate 'volatile' type qualifier");
        type->is_volatile = true;
        next_token();
        break;
    default:
        break;
    }
}

struct decltype *signedness_specifier(struct decltype *type)
{
    switch (token) {
    case T_SIGNED:
        if (type->signedness == T_UNSIGNED)
            error("cannot combine unsigned and signed in a declaration");
        else if (type->signedness = T_SIGNED)
            error("duplicate signed in a declaration");
        type->signedness = T_SIGNED;
        next_token();
        break;
    case T_UNSIGNED:
        if (type->signedness == T_SIGNED)
            error("cannot combine signed and unsigned in a declaration");
        else if (type->signedness = T_UNSIGNED)
            error("duplicate unsigned in a declaration");
        type->signedness = T_UNSIGNED;
        next_token();
        break;
    default:
        break;
    }

    return type;
}

struct decltype *type_specifier(struct decltype *type)
{
    if (type->base)
        error("multiple type specifier");

    if (token == T_IDENTIFIER) {
        typedef_base = lookup_symtab(current_scope, token_string);
        if (typedef_base) {
            type->typedef_base = typedef_base; // base, qualifier, signedness
            type->base = TYPEDEF_NAME;
        }
    } else {
        type = signedness_specifier(type);
        switch (token) {
        case T_VOID:
            if (type->signedness)
                error("signed/unsigned on void is not allowed");
            // fallthrough
        case T_CHAR:
        case T_INT:
            type->base = token;
            next_token();
            break;
        case T_SHORT:
        case T_SHORT_INT:
        case T_INT_SHORT:
            type->base = T_SHORT;
            next_token();
            break;
        case T_LONG:
        case T_LONG_INT:
        case T_INT_LONG:
            type->base = T_LONG;
            next_token();
            break;
        case T_FLOAT:
        case T_DOUBLE:
        case T_LONG_DOUBLE:
        case T_DOUBLE_LONG:
            if (type->signedness)
                error("signed/unsigned on floating type is not allowed");
            type->base = token;
            next_token();
            break;
        case T_ENUM:
            if (type->signedness)
                error("signed/unsigned on enum is not allowed");
            type = enum_specifier();
            break;
        case T_STRUCT:
        case T_UNION:
            if (type->signedness)
                error("signed/unsigned on struct/union is not allowed");
            type = struct_or_union_specifier();
            break;
        }
    }

    return type;
}

bool valid_declspec(enum tokentype token)
{
    switch (token) {
    case T_TYPEDEF:
    case T_EXTERN:
    case T_STATIC:
    case T_AUTO:
    case T_REGISTER:
    case T_CONST:
    case T_VOLATILE:
    case T_SIGNED:
    case T_UNSIGNED:
    case T_VOID:
    case T_CHAR:
    case T_SHORT:
    case T_SHORT_INT:
    case T_INT_SHORT:
    case T_LONG:
    case T_LONG_INT:
    case T_INT_LONG:
    case T_FLOAT:
    case T_DOUBLE:
    case T_LONG_DOUBLE:
    case T_DOUBLE_LONG:
        return true;
    }
    return false;
}

struct decltype *declaration_specifier(void)
{
    struct decltype *type = new_type();

    do {
        type = storage_class_specifier(type);
        type = type_qualifier(type);
        type = type_specifier(type);
    } while (valid_declspec(token) || type->base == TYPEDEF_NAME);

    if (token == T_EOF)
        error("unexpected end of file");

    return type;
}

struct decltree *declaration(void)
{
    struct decltree *tree = new_decltree();

    while (token != ';') {
        struct decltype *type = declaration_specifier();
        if (type->storage_class = T_TYPEDEF) {
            struct declaration *decl = declarator(type);
            if (lookup_symtab(current_scope, decl->name))
                error("identifier was previously defined");
            insert_symtab(current_scope, decl->name, decl);
        } else {
            tree = init_declarator_list(type, tree);
        }
    }

    return tree;
}
