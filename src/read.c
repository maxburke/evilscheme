/***********************************************************************
 * evilscheme, Copyright (c) 2012-2015, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "base.h"
#include "evil_scheme.h"
#include "gc.h"
#include "object.h"
#include "runtime.h"
#include "slist.h"

/*
 * The input stream is broken up into a number of coarse-grained tokens,
 * parentheses to denote the begin/end of lists, symbols which include both
 * symbols and numbers, and strings. Shortcuts like ' (quote), ` (quasiquote),
 * , (unquote), and ,@ (unquote-splicing) are expanded into their s-exp
 * equivalents before being parsed.
 */
enum token_type_t
{
    TOKEN_INVALID,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_SYMBOL,
    TOKEN_STRING
};

enum token_fields_t
{
    TOKEN_FIELD_LINK,
    TOKEN_FIELD_TYPE,
    TOKEN_FIELD_TEXT,
    TOKEN_FIELD_NUM
};

static const char *
consume_whitespace(const char *input)
{
    while (*input == ' ' || *input == '\t' || *input == '\r' || *input == '\n')
        ++input;

    return input;
}

static const char *
find_current_token_end(const char **input_ptr, enum token_type_t *token_type)
{
    const char *input;

    input = *input_ptr;

    if (*input == '(' || *input == '[' || *input == '{')
    {
        *token_type = TOKEN_LPAREN;
        return input + 1;
    }

    if (*input == ')' || *input == ']' || *input == '}')
    {
        *token_type = TOKEN_RPAREN;
        return input + 1;
    }

    if (*input == '"')
    {
        *token_type = TOKEN_STRING;
        ++*input_ptr;

        while (*++input != '"')
            ;
        return input;
    }

    *token_type = TOKEN_SYMBOL;
    while (*input && *input != '(' && *input != ')' && !isspace(*input))
        ++input;

    return input;
}

/*
 * When parsing numbers only the first one or two characters are examined,
 * as that is enough to determine whether or not the token is a number.
 */
static int
is_number(struct evil_object_t *string_object)
{
    const char *string;

    assert(string_object->tag_count.tag == TAG_STRING);
    string = string_object->value.string_value;

    if (string[0] >= '0' && string[0] <= '9')
        return 1;

    if ((string[0] == '-' || string[0] == '+')
            && (string[1] >= '0' && string[1] <= '9'))
        return 1;

    return 0;
}

/*
 * Given a string representation, create a scheme object from it. This will
 * change strings like "#t" into their equivalent machine representation
 * of a Scheme true value.
 */
static struct evil_object_t
object_from_symbol(struct evil_environment_t *environment, struct evil_object_handle_t *handle)
{
    size_t string_length;
    struct evil_object_t *token;
    struct evil_object_t *string_object;
    const char *string;
    int64_t type;

    token = evil_resolve_object_handle(handle);
    string_object = deref(&VECTOR_BASE(token)[TOKEN_FIELD_TEXT]);
    assert(string_object->tag_count.tag == TAG_STRING);

    string = string_object->value.string_value;
    string_length = strlen(string);

    if (string[0] == '#')
    {
        struct evil_object_t object;
        const char *char_ptr;

        /*
         * #t/#f -> boolean
         */
        char next_char = (char)tolower(string[1]);
        if (next_char == 't' || next_char == 'f')
        {
            object.tag_count.tag = TAG_BOOLEAN;
            object.tag_count.flag = 0;
            object.tag_count.count = 1;
            object.value.fixnum_value = next_char == 't';

            return object;
        }

        /*
         * #\blat -> char
         */
        assert(next_char == '\\');
        object.tag_count.tag = TAG_CHAR;
        object.tag_count.flag = 0;
        object.tag_count.count = 1;
        char_ptr = string + 2;
        object.value.fixnum_value = strstr(string, "newline") != NULL ? '\n' : *char_ptr;
        return object;
    }
    else if (is_number(string_object))
    {
        struct evil_object_t object;

        if (strstr(string, ".") == NULL)
        {
            object.tag_count.tag = TAG_FIXNUM;
            object.tag_count.flag = 0;
            object.tag_count.count = 1;
            object.value.fixnum_value = strtol(string, NULL, 0);
        }
        else
        {
            object.tag_count.tag = TAG_FLONUM;
            object.tag_count.flag = 0;
            object.tag_count.count = 1;
            object.value.flonum_value = strtod(string, NULL);
        }

        return object;
    }

    /*
     * If it's not a boolean, char, or number that we've parsed then it must
     * be a symbol or a string. Vectors, pairs, and procedures are allocated
     * at runtime.
     */
    string_length = strlen(string);
    type = VECTOR_BASE(token)[TOKEN_FIELD_TYPE].value.fixnum_value;

    if (type == TOKEN_STRING)
    {
        /*
         * We already have a string object here, why create a new one
         * unnecessarily?
         */
        return make_ref(string_object);
    }
    else
    {
        struct evil_object_t object;

        assert(type == TOKEN_SYMBOL);
        object.tag_count.tag = TAG_SYMBOL;
        object.tag_count.flag = 0;
        object.tag_count.count = 1;
        object.value.symbol_hash = register_symbol_from_bytes(
                environment,
                string,
                string_length);
        return object;
    }
}

static struct evil_object_t
recursive_create_object_from_token_stream(struct evil_environment_t *environment, struct evil_object_handle_t *input)
{
    struct evil_object_t *object;
    int64_t type;

    object = deref(evil_resolve_object_handle(input));

    if (object == empty_pair)
        return make_empty_ref();

    type = VECTOR_BASE(object)[TOKEN_FIELD_TYPE].value.fixnum_value;

    if (type == TOKEN_LPAREN)
    {
        struct evil_object_handle_t *pair_handle;
        struct evil_object_t *pair;
        struct evil_object_t *next;
        struct evil_object_t result;

        pair = gc_alloc(environment->heap, TAG_PAIR, 0);
        pair_handle = evil_create_object_handle(environment, pair);
        object = deref(evil_resolve_object_handle(input));
        next = deref(&VECTOR_BASE(object)[TOKEN_FIELD_LINK]);
        evil_retarget_object_handle(input, next);

        result = recursive_create_object_from_token_stream(environment, input);
        pair = evil_resolve_object_handle(pair_handle);
        *RAW_CAR(pair) = result;

        if (deref(evil_resolve_object_handle(input)) == empty_pair)
        {
            *RAW_CDR(pair) = make_empty_ref();
        }
        else
        {
            result = recursive_create_object_from_token_stream(environment, input);
            pair = evil_resolve_object_handle(pair_handle);
            *RAW_CDR(pair) = result;
        }

        evil_destroy_object_handle(environment, pair_handle);
        return make_ref(pair);
    }
    else if (type == TOKEN_RPAREN)
    {
        struct evil_object_t *next;

        object = deref(evil_resolve_object_handle(input));
        next = deref(&VECTOR_BASE(object)[TOKEN_FIELD_LINK]);
        evil_retarget_object_handle(input, next);

        return make_empty_ref();
    }
    else
    {
        struct evil_object_t *next;
        struct evil_object_t *pair;
        struct evil_object_handle_t *pair_handle;
        struct evil_object_t result;

        pair = gc_alloc(environment->heap, TAG_PAIR, 0);
        pair_handle = evil_create_object_handle(environment, pair);

        result = object_from_symbol(environment, input);
        pair = evil_resolve_object_handle(pair_handle);
        *RAW_CAR(pair) = result;

        next = deref(&VECTOR_BASE(object)[TOKEN_FIELD_LINK]);
        evil_retarget_object_handle(input, next);

        result = recursive_create_object_from_token_stream(environment, input);
        pair = evil_resolve_object_handle(pair_handle);
        *RAW_CDR(pair) = result;

        return make_ref(pair);
    }
}

static struct evil_object_t
create_object_from_token_stream(struct evil_environment_t *environment, struct evil_object_handle_t *input)
{
    return recursive_create_object_from_token_stream(environment, input);
}

static struct evil_object_t
make_string_object(struct evil_environment_t *environment, const char *string)
{
    struct evil_object_t *string_object;
    size_t string_length;

    string_length = strlen(string);
    string_object = gc_alloc(environment->heap, TAG_STRING, string_length);
    memmove(string_object->value.string_value, string, string_length + 1);

    return make_ref(string_object);
}

/*
 * Given a token in the stream, expand it to the specified expansion and
 * add the appropriate matching parentheses.
 */
static void
expand_to(struct evil_environment_t *environment, struct evil_object_handle_t *handle, const char *expansion)
{
    struct evil_object_t *current;
    struct evil_object_t *token;
    struct evil_object_t *expansion_token;
    struct evil_object_t *matching_rparen;
    struct evil_object_handle_t *expansion_token_handle;
    struct evil_object_handle_t *matching_rparen_handle;
    struct evil_object_t expansion_string;
    struct evil_object_t rparen_string;
    struct evil_object_t lparen_string;
    size_t paren_level;
    int64_t token_type;

    paren_level = 0;

    /*
     * Allocate expansion token
     */
    expansion_token = gc_alloc_vector(environment->heap, TOKEN_FIELD_NUM);
    expansion_token_handle = evil_create_object_handle(environment, expansion_token);
    expansion_string = make_string_object(environment, expansion);
    expansion_token = evil_resolve_object_handle(expansion_token_handle);
    VECTOR_BASE(expansion_token)[TOKEN_FIELD_TYPE] = make_fixnum_object(TOKEN_SYMBOL);
    VECTOR_BASE(expansion_token)[TOKEN_FIELD_TEXT] = expansion_string;

    /*
     * Allocate matching rparen token
     */
    matching_rparen = gc_alloc_vector(environment->heap, TOKEN_FIELD_NUM);
    matching_rparen_handle = evil_create_object_handle(environment, matching_rparen);
    rparen_string = make_string_object(environment, ")");
    matching_rparen = evil_resolve_object_handle(matching_rparen_handle);
    VECTOR_BASE(matching_rparen)[TOKEN_FIELD_TYPE] = make_fixnum_object(TOKEN_RPAREN);
    VECTOR_BASE(matching_rparen)[TOKEN_FIELD_TEXT] = rparen_string;

    /*
     * 1. Change current token to an lparen
     */
    lparen_string = make_string_object(environment, "(");
    token = evil_resolve_object_handle(handle);
    VECTOR_BASE(token)[TOKEN_FIELD_TYPE] = make_fixnum_object(TOKEN_LPAREN);
    VECTOR_BASE(token)[TOKEN_FIELD_TEXT] = lparen_string;

    /*
     * 2. Insert the expansion symbol.
     */
    expansion_token = evil_resolve_object_handle(expansion_token_handle);
    VECTOR_BASE(expansion_token)[TOKEN_FIELD_LINK] = VECTOR_BASE(token)[TOKEN_FIELD_LINK];
    VECTOR_BASE(token)[TOKEN_FIELD_LINK] = make_ref(expansion_token);

    /*
     * 3. Find matching end paren location, if it is necessary to do so.
     */
    current = deref(&VECTOR_BASE(expansion_token)[TOKEN_FIELD_LINK]);
    assert(current->tag_count.tag == TAG_VECTOR);
    token_type = VECTOR_BASE(current)[TOKEN_FIELD_TYPE].value.fixnum_value;

    if (token_type == TOKEN_LPAREN)
    {
        while (current != empty_pair)
        {
            token_type = VECTOR_BASE(current)[TOKEN_FIELD_TYPE].value.fixnum_value;

            if (token_type == TOKEN_LPAREN)
            {
                ++paren_level;
            }
            else if (token_type == TOKEN_RPAREN)
            {
                --paren_level;

                if (paren_level == 0)
                {
                    current = deref(&VECTOR_BASE(current)[TOKEN_FIELD_LINK]);
                    break;
                }
            }

            current = deref(&VECTOR_BASE(current)[TOKEN_FIELD_LINK]);
        }
    }

    matching_rparen = evil_resolve_object_handle(matching_rparen_handle);
    VECTOR_BASE(matching_rparen)[TOKEN_FIELD_LINK] = VECTOR_BASE(current)[TOKEN_FIELD_LINK];
    VECTOR_BASE(current)[TOKEN_FIELD_LINK] = make_ref(matching_rparen);
}

static void
split_symbol_token(struct evil_environment_t *environment, struct evil_object_handle_t *handle, int text_index)
{
    struct evil_object_t *token;
    struct evil_object_t *string_object;
    struct evil_object_t *new_token;
    struct evil_object_t *new_string;
    struct evil_object_handle_t *new_token_handle;
    char *string;
    size_t token_text_length;

    token = evil_resolve_object_handle(handle);
    assert(token->tag_count.tag == TAG_VECTOR);
    string_object = deref(&VECTOR_BASE(token)[TOKEN_FIELD_TEXT]);
    string = string_object->value.string_value;

    token_text_length = strlen(string);

    if (string[text_index] == '\0')
        return;

    assert(VECTOR_BASE(token)[TOKEN_FIELD_TYPE].value.fixnum_value == TOKEN_SYMBOL);

    new_token = gc_alloc_vector(environment->heap, TOKEN_FIELD_NUM);
    new_token_handle = evil_create_object_handle(environment, new_token);
    new_string = gc_alloc(environment->heap, TAG_STRING, token_text_length - text_index);

    new_token = evil_resolve_object_handle(new_token_handle);
    evil_destroy_object_handle(environment, new_token_handle);

    /*
     * Refresh our token pointer in case the GC moved it.
     */
    token = evil_resolve_object_handle(handle);
    string_object = deref(&VECTOR_BASE(token)[TOKEN_FIELD_TEXT]);
    string = string_object->value.string_value;

    memmove(new_string->value.string_value, &string[text_index], token_text_length - text_index);

    VECTOR_BASE(new_token)[TOKEN_FIELD_TYPE] = make_fixnum_object(TOKEN_SYMBOL);
    VECTOR_BASE(new_token)[TOKEN_FIELD_TEXT] = make_ref(new_string);
    VECTOR_BASE(new_token)[TOKEN_FIELD_LINK] = VECTOR_BASE(token)[TOKEN_FIELD_LINK];
    VECTOR_BASE(token)[TOKEN_FIELD_LINK] = make_ref(new_token);

    string[text_index] = '\0';
}

/*
 * Perform the expansion of shortcut tokens (' ` , ,@ #) to their s-exp forms
 * (quote, quasiquote, unquote, unquote-splicing, vector).
 */
static struct evil_object_handle_t *
apply_expansions(struct evil_environment_t *environment, struct evil_object_handle_t *input)
{
    struct evil_object_handle_t *handle;
    struct evil_object_t *iter;

    /*
     * We need a spare handle for the calls to split_symbol_token/eexpand_to
     * and it's just easier to do it with the object we're given as input,
     * instead of creating/boxing a new object.
     */
    iter = evil_resolve_object_handle(input);
    handle = evil_create_object_handle(environment, iter);

    while ((iter = deref(iter)) != empty_pair)
    {
        char *string;

        string = deref(&VECTOR_BASE(iter)[TOKEN_FIELD_TEXT])->value.string_value;
        evil_retarget_object_handle(handle, iter);

        if (string[0] == '\'')
        {
            /*
             * 'EXPR -> (quote EXPR)
             */
            split_symbol_token(environment, handle, 1);
            expand_to(environment, handle, "quote");
        }
        else if (string[0] == '`')
        {
            /*
             * `TEMPLATE -> (quasiquote TEMPLATE)
             */
            split_symbol_token(environment, handle, 1);
            expand_to(environment, handle, "quasiquote");
        }
        else if (string[0] == '#' && string[1] == '\0')
        {
            struct evil_object_t *next;
            struct evil_object_t vector_symbol;
            struct evil_object_handle_t *vector_handle;

            /*
             * Repurpose the symbol ('#') to a lparen ('(').
             */
            VECTOR_BASE(iter)[TOKEN_FIELD_TYPE] = make_fixnum_object(TOKEN_LPAREN);
            string[0] = '(';

            next = deref(&VECTOR_BASE(iter)[TOKEN_FIELD_LINK]);
            assert(VECTOR_BASE(next)[TOKEN_FIELD_TYPE].value.fixnum_value == TOKEN_LPAREN);
            VECTOR_BASE(next)[TOKEN_FIELD_TYPE] = make_fixnum_object(TOKEN_SYMBOL);

            vector_handle = evil_create_object_handle(environment, next);
            vector_symbol = make_string_object(environment, "vector");
            next = evil_resolve_object_handle(vector_handle);
            VECTOR_BASE(next)[TOKEN_FIELD_TEXT] = vector_symbol;
        }
        else if (string[0] == ',')
        {
            if (string[1] == '@')
            {
                split_symbol_token(environment, handle, 2);
                expand_to(environment, handle, "unquote-splicing");
            }
            else
            {
                split_symbol_token(environment, handle, 1);
                expand_to(environment, handle, "unquote");
            }
        }

        /*
         * As the expansions above may trigger a collect, we need to re-resolve
         * the object handle here.
         */
        iter = evil_resolve_object_handle(handle);
        iter = deref(&VECTOR_BASE(iter)[TOKEN_FIELD_LINK]);
    }

    return input;
}

static struct evil_object_handle_t *
reverse(struct evil_environment_t *environment, struct evil_object_handle_t *head)
{
    struct evil_object_t last;
    struct evil_object_t *object;

    last = make_empty_ref();
    object = evil_resolve_object_handle(head);
    while ((object = deref(object)) != empty_pair)
    {
        struct evil_object_t *temp;

        assert(object->tag_count.tag == TAG_VECTOR);

        temp = deref(&VECTOR_BASE(object)[TOKEN_FIELD_LINK]);
        VECTOR_BASE(object)[TOKEN_FIELD_LINK] = last;
        last = make_ref(object);
        object = temp;
    }

    return evil_create_object_handle_from_value(environment, last);
}

/*
 * Validates the current token stream and returns NULL if the number of
 * parentheses are not balanced.
 */
static struct evil_object_handle_t *
validate(struct evil_environment_t *environment, struct evil_object_handle_t *head)
{
    int paren_level;
    struct evil_object_t *ptr;

    paren_level = 0;
    ptr = evil_resolve_object_handle(head);

    while ((ptr = deref(ptr)) != empty_pair)
    {
        int64_t type;

        assert(ptr->tag_count.tag == TAG_VECTOR);
        type = VECTOR_BASE(ptr)[TOKEN_FIELD_TYPE].value.fixnum_value;

        if (type == TOKEN_LPAREN)
        {
            ++paren_level;
        }
        else if (type == TOKEN_RPAREN)
        {
            --paren_level;
        }

        ptr = deref(&VECTOR_BASE(ptr)[TOKEN_FIELD_LINK]);
    }

    if (paren_level == 0)
    {
        return head;
    }

    evil_destroy_object_handle(environment, head);
    return NULL;
}

static struct evil_object_handle_t *
tokenize(struct evil_environment_t *environment, const char *input)
{
    struct evil_object_handle_t *end;

    end = evil_create_object_handle_from_value(environment, make_empty_ref());

    while (*input)
    {
        enum token_type_t token_type;
        const char *current_token_end;
        struct evil_object_handle_t *token_handle;
        struct evil_object_t *token;
        struct evil_object_t *string;

        current_token_end = find_current_token_end(&input, &token_type);

        token = gc_alloc_vector(environment->heap, TOKEN_FIELD_NUM);
        token_handle = evil_create_object_handle(environment, token);
        string = gc_alloc(environment->heap, TAG_STRING, current_token_end - input);
        token = evil_resolve_object_handle(token_handle);
        evil_destroy_object_handle(environment, token_handle);

        memmove(string->value.string_value, input, current_token_end - input);
        VECTOR_BASE(token)[TOKEN_FIELD_TYPE] = make_fixnum_object(token_type);
        VECTOR_BASE(token)[TOKEN_FIELD_TEXT] = make_ref(string);

        /*
         * Skip over any trailing quotation marks that make up the string
         * literals.
         */
        if (token_type == TOKEN_STRING)
        {
            ++current_token_end;
        }

        input = consume_whitespace(current_token_end);
        VECTOR_BASE(token)[TOKEN_FIELD_LINK] = make_ref(evil_resolve_object_handle(end));
        evil_retarget_object_handle(end, token);
    }

    return apply_expansions(environment, validate(environment, reverse(environment, end)));
}

struct evil_object_t
evil_read(struct evil_environment_t *environment, struct evil_object_handle_t *lexical_environment, int num_args, struct evil_object_t *args)
{
    struct evil_object_t *arg;
    struct evil_object_handle_t *head;

    UNUSED(environment);
    UNUSED(lexical_environment);

    assert(num_args == 1);

    arg = deref(args);
    assert(arg->tag_count.tag == TAG_STRING);

    head = tokenize(environment, arg->value.string_value);
    return create_object_from_token_stream(environment, head);
}

