/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license. 
 * See LICENSE.TXT for details.
 ***********************************************************************/

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "base.h"
#include "gc.h"
#include "object.h"
#include "read.h"
#include "runtime.h"
#include "slist.h"

/*
 * TODO: Remove use of calloc.
 */

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

struct token_t 
{
    struct slist_t link;
    enum token_type_t type;
    const char text[2];
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
    while (*input && *input != '(' && *input != ')' && *input != ' ')
        ++input;

    return input;
}

/*
 * When parsing numbers only the first one or two characters are examined,
 * as that is enough to determine whether or not the token is a number.
 */
static int 
is_number(const struct token_t *input)
{
    if (input->text[0] >= '0' && input->text[0] <= '9')
        return 1;

    if ((input->text[0] == '-' || input->text[0] == '+')
            && (input->text[1] >= '0' && input->text[1] <= '9'))
        return 1;

    return 0;
}

/*
 * Given a string representation, create a lisp object from it. This will
 * change strings like "#t" into their equivalent machine representation
 * of a Scheme true value.
 */
static struct object_t
object_from_symbol(struct environment_t *environment, const struct token_t *input)
{
    size_t string_length;

    if (input->text[0] == '#') 
    {
        struct object_t object;

        /* 
         * #t/#f -> boolean
         */
        char next_char = (char)tolower(input->text[1]);
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
        object.value.fixnum_value = strstr(input->text, "newline") != NULL ? '\n' : input->text[2];
        return object;
    }
    else if (is_number(input)) 
    {
        struct object_t object;

        if (strstr(input->text, ".") == NULL)
        {
            object.tag_count.tag = TAG_FIXNUM;
            object.tag_count.flag = 0;
            object.tag_count.count = 1;
            object.value.fixnum_value = strtol(input->text, NULL, 0);
        }
        else
        {
            object.tag_count.tag = TAG_FLONUM;
            object.tag_count.flag = 0;
            object.tag_count.count = 1;
            object.value.flonum_value = strtod(input->text, NULL);
        }

        return object;
    }

    /*
     * If it's not a boolean, char, or number that we've parsed then it must 
     * be a symbol or a string. Vectors, pairs, and procedures are allocated
     * at runtime.
     */
    string_length = strlen(input->text);

    if (input->type == TOKEN_STRING)
    {
        struct object_t *object;

        object = gc_alloc(environment->heap, TAG_STRING, string_length);
        memmove(object->value.string_value, input->text, string_length);

        return make_ref(object);
    }
    else
    {
        struct object_t object;

        assert(input->type == TOKEN_SYMBOL);
        object.tag_count.tag = TAG_SYMBOL;
        object.tag_count.flag = 0;
        object.tag_count.count = 1;
        object.value.symbol_hash = register_symbol_from_bytes(
                environment, 
                input->text, 
                string_length);
        return object;
    }
}

static struct object_t
recursive_create_object_from_token_stream(struct environment_t *environment, const struct token_t **input)
{
    if (*input == NULL)
        return make_empty_ref();

    if ((*input)->type == TOKEN_LPAREN)
    {
        struct object_t *pair = gc_alloc(environment->heap, TAG_PAIR, 0);
        *input = (const struct token_t *)(*input)->link.next;

        *RAW_CAR(pair) = recursive_create_object_from_token_stream(environment, input);

        if (*input)
        {
            *RAW_CDR(pair) = recursive_create_object_from_token_stream(environment, input);
        }
        else
        {
            *RAW_CDR(pair) = make_empty_ref();
        }

        return make_ref(pair);
    }
    else if ((*input)->type == TOKEN_RPAREN)
    {
        *input = (const struct token_t *)(*input)->link.next;
        return make_empty_ref();
    }
    else
    {
        struct object_t *pair = gc_alloc(environment->heap, TAG_PAIR, 0);
        *RAW_CAR(pair) = object_from_symbol(environment, *input);

        *input = (const struct token_t *)(*input)->link.next;
        *RAW_CDR(pair) = recursive_create_object_from_token_stream(environment, input);
        return make_ref(pair);
    }
}

static struct object_t
create_object_from_token_stream(struct environment_t *environment, const struct token_t *input)
{
    return recursive_create_object_from_token_stream(environment, &input);
}

/*
 * Given a token in the stream, expand it to the specified expansion and
 * add the appropriate matching parentheses.
 */
static void 
expand_to(struct token_t *input, const char *expansion)
{
    struct token_t *expansion_token;
    struct token_t *matching_rparen;
    struct token_t *current;
    size_t expansion_string_length;
    size_t paren_level;
    
    expansion_string_length = strlen(expansion);
    paren_level = 0;

    /*
     * Allocate expansion token
     */
    expansion_token = calloc(sizeof(struct token_t) + expansion_string_length, 1);
    expansion_token->type = TOKEN_SYMBOL;
    memmove((void *)(&expansion_token->text), expansion, expansion_string_length);

    /*
     * Allocate matching rparen token
     */
    matching_rparen = calloc(sizeof(struct token_t) + strlen(expansion), 1);
    matching_rparen->type = TOKEN_RPAREN;
    ((char *)matching_rparen->text)[0] = ')';

    /*
     * 1. change current token to an lparen
     */
    input->type = TOKEN_LPAREN;
    ((char *)input->text)[0] = '(';
    ((char *)input->text)[1] = '\0';

    /*
     * 2. insert the expansion symbol
     */
    expansion_token->link.next = input->link.next;
    input->link.next = (struct slist_t *)expansion_token;

    /*
     * 3. find matching end paren location, if it is necessary to do so.
     */
    current = (struct token_t *)expansion_token->link.next;

    if (current->type == TOKEN_LPAREN)
        for (;;)
        {
            if (current->type == TOKEN_LPAREN) 
                ++paren_level;

            if (current->type == TOKEN_RPAREN)
            {
                --paren_level;

                if (paren_level == 0)
                    break;
            }

            current = (struct token_t *)current->link.next;
        }

    /*
     * 4. insert rparen.
     */
    matching_rparen->link.next = current->link.next;
    current->link.next = (struct slist_t *)matching_rparen;
}

static void
split_symbol_token(struct token_t *input, int text_index)
{
    struct token_t *new_token;
    size_t token_text_length;
    
    token_text_length = strlen(input->text);

    if (input->text[text_index] == '\0')
        return;
    
    assert(input->type == TOKEN_SYMBOL);

    new_token = calloc(sizeof(struct token_t) + token_text_length - text_index, 1);
    new_token->type = TOKEN_SYMBOL;
    memmove((void *)(&new_token->text), &input->text[text_index], token_text_length - text_index);
    new_token->link.next = input->link.next;
    input->link.next = (struct slist_t *)new_token;

    *((char *)&input->text[text_index]) = '\0';
}

/*
 * Perform the expansion of shortcut tokens (' ` , ,@) to their s-exp forms
 * (quote, quasiquote, unquote, unquote-splicing).
 */
static struct token_t *
apply_expansions(struct token_t *input)
{
    struct token_t *iter;
    
    iter = input;

    while (iter)
    {
        if (iter->text[0] == '\'')
        {
            /*
             * 'EXPR -> (quote EXPR)
             */
            split_symbol_token(iter, 1);
            expand_to(iter, "quote");
        }
        else if (iter->text[0] == '`')
        {
            /*
             * `TEMPLATE -> (quasiquote TEMPLATE)
             */
            split_symbol_token(iter, 1);
            expand_to(iter, "quasiquote");
        }
        else if (iter->text[0] == '#')
        {
            struct token_t *vector_token;
            struct slist_t *next = iter->link.next->next;

            /*
             * #(CONTENTS) -> (vector CONTENTS)
             * Change current token to an LPAREN
             */
            assert(iter->text[1] == '\0');
            iter->type = TOKEN_LPAREN;
            ((char *)iter->text)[0] = '(';

            vector_token = realloc(iter->link.next, sizeof(struct token_t) + 6 /* strlen("vector") */);
            vector_token->link.next = next;
            vector_token->type = TOKEN_SYMBOL;
            memmove((void *)(vector_token->text), "vector", 7);
            iter->link.next = (struct slist_t *)vector_token;
        }
        else if (iter->text[0] == ',') 
        {
            if (iter->text[1] == '@')
            {
                split_symbol_token(iter, 2);
                expand_to(iter, "unquote-splicing");
            }
            else
            {
                split_symbol_token(iter, 1);
                expand_to(iter, "unquote");
            }
        }

        iter = (struct token_t *)iter->link.next;
    }
     
    return input;
}

/*
 * Validates the current token stream and returns NULL if the number of
 * parentheses are not balanced.
 */
static struct token_t *
validate(struct token_t *head)
{
    int paren_level;
    const struct token_t *ptr;

    paren_level = 0;
    ptr = head;

    while (ptr != NULL)
    {
        if (ptr->type == TOKEN_LPAREN) ++paren_level;
        if (ptr->type == TOKEN_RPAREN) --paren_level;
        ptr = (const struct token_t *)ptr->link.next;
    }

    return paren_level == 0 ? head : NULL;
}

static struct token_t *
tokenize(const char *input)
{
    struct token_t *head;
    
    head = NULL;

    while (*input) 
    {
        enum token_type_t token_type;
        const char *current_token_end = find_current_token_end(&input, &token_type);
        struct token_t *token = calloc(sizeof(struct token_t) + (current_token_end - input), 1);
        memmove((void *)(&token->text), input, current_token_end - input);
        token->type = token_type;

        if (token_type == TOKEN_STRING) 
            ++current_token_end;

        input = consume_whitespace(current_token_end);
        slist_link(&token->link, (struct slist_t *)head);
        head = token;
    }

    return apply_expansions(validate((struct token_t *)slist_reverse(&head->link)));
}

struct object_t
read(struct environment_t *environment, struct object_t *args)
{
    struct object_t *object;
    struct object_t *arg;
    struct token_t *head;

    object = deref(args);

    UNUSED(environment);
    assert(object->tag_count.tag == TAG_PAIR);

    arg = CAR(object);
    assert(arg->tag_count.tag == TAG_STRING);

    head = tokenize(arg->value.string_value);
    return create_object_from_token_stream(environment, head);
}

