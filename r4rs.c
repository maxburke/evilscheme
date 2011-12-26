#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _MSC_VER
#   include <Windows.h>
#   define BREAK() __asm int 3
#else
#   include <signal.h>
#   define BREAK() raise(SIGTRAP)
#   define UNUSED(x) (void)x
#endif

/*
 * Type forward declarations
 */
struct object_t;
struct environment_t;

#define CAR(x) ((x)->pair[0])
#define CDR(x) ((x)->pair[1])

/*
 * Built-in object prototypes
 */
static struct object_t *
read(struct environment_t *environment, struct object_t *object);

static struct object_t *
eval(struct environment_t *environment, struct object_t *object);

static struct object_t *
print(struct environment_t *environment, struct object_t *object);

static struct object_t *
cons(struct environment_t *environment, struct object_t *object);

static struct object_t *
car(struct environment_t *environment, struct object_t *object);

static struct object_t *
cdr(struct environment_t *environment, struct object_t *object);

static struct object_t *
define(struct environment_t *environment, struct object_t *object);

static struct object_t *
set(struct environment_t *environment, struct object_t *object);

static struct object_t *
quote(struct environment_t *environment, struct object_t *object);

static struct object_t *
lambda(struct environment_t *environment, struct object_t *object);

/*
 * Singly linked list
 */
struct slist_t
{
    struct slist_t *next;
};

static void 
slist_link(struct slist_t *from, struct slist_t *to)
{
    from->next = to;
}

static struct slist_t *
slist_reverse(struct slist_t *head)
{
    struct slist_t *last = NULL;

    while (head != NULL) 
    {
        struct slist_t *temp = head->next;
        head->next = last;
        last = head;
        head = temp;
    }

    return last;
}

/*
 * Token structures and generation code
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
    const char *input = *input_ptr;

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

static struct token_t *
validate(struct token_t *head)
{
    int paren_level = 0;
    const struct token_t *ptr = head;

    while (ptr != NULL)
    {
        if (ptr->type == TOKEN_LPAREN) ++paren_level;
        if (ptr->type == TOKEN_RPAREN) --paren_level;
        ptr = (const struct token_t *)ptr->link.next;
    }

    return paren_level == 0 ? head : NULL;
}

static void
split_symbol_token(struct token_t *input, int text_index)
{
    struct token_t *new_token;
    size_t token_text_length = strlen(input->text);

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

static void 
expand_to(struct token_t *input, const char *expansion)
{
    struct token_t *expansion_token;
    struct token_t *matching_rparen;
    struct token_t *current;
    size_t expansion_string_length = strlen(expansion);
    size_t paren_level = 0;

    /* Allocate expansion token */
    expansion_token = calloc(sizeof(struct token_t) + expansion_string_length, 1);
    expansion_token->type = TOKEN_SYMBOL;
    memmove((void *)(&expansion_token->text), expansion, expansion_string_length);

    /* Allocate matching rparen token */
    matching_rparen = calloc(sizeof(struct token_t) + strlen(expansion), 1);
    matching_rparen->type = TOKEN_RPAREN;
    ((char *)matching_rparen->text)[0] = ')';

    /* 1. change current token to an lparen */
    input->type = TOKEN_LPAREN;
    ((char *)input->text)[0] = '(';
    ((char *)input->text)[1] = '\0';

    /* 2. insert the expansion symbol */
    expansion_token->link.next = input->link.next;
    input->link.next = (struct slist_t *)expansion_token;

    /* 3. find matching end paren location, if it is necessary to do so.*/
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

    /* 4. insert rparen. */
    matching_rparen->link.next = current->link.next;
    current->link.next = (struct slist_t *)matching_rparen;
}

static struct token_t *
apply_expansions(struct token_t *input)
{
    struct token_t *iter = input;

    while (iter)
    {
        if (iter->text[0] == '\'')
        {
            /* 'EXPR -> (quote EXPR) */
            split_symbol_token(iter, 1);
            expand_to(iter, "quote");
        }
        else if (iter->text[0] == '`')
        {
            /* `TEMPLATE -> (quasiquote TEMPLATE) */
            split_symbol_token(iter, 1);
            expand_to(iter, "quasiquote");
        }
        else if (iter->text[0] == '#')
        {
            struct token_t *vector_token;
            struct slist_t *next = iter->link.next->next;

            /* #(CONTENTS) -> (vector CONTENTS) */
            /* Change current token to an LPAREN */
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

static struct token_t *
tokenize(const char *input)
{
    struct token_t *head = NULL;

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

/*
 * Parse code
 */

enum tag_t
{
    TAG_INVALID,
    TAG_BOOLEAN,
    TAG_SYMBOL,
    TAG_CHAR,
    TAG_VECTOR,
    TAG_PAIR,
    TAG_FIXNUM,
    TAG_FLONUM,
    TAG_STRING,
    TAG_PROCEDURE,
    TAG_SPECIAL_FUNCTION
};

struct tag_count_t
{
    unsigned tag : 4;
    unsigned count : 16;
};

struct object_t;
struct environment_t;
typedef struct object_t *(*special_function_t)(struct environment_t *, struct object_t *);

struct object_t
{
    struct tag_count_t tag_count;
    union
    {
        int boolean_value;
        long fixnum_value;
        double flonum_value;
        char char_value;
        special_function_t special_function_value;
        char string_value[1];
        struct object_t *pair[2];
    };
};

/*
 * Environment related data structures and functions
 */

#define NUM_ENTRIES_PER_FRAGMENT 16

struct symbol_table_entry_t
{
    struct object_t *symbol;
    struct object_t *object;
};

struct symbol_table_fragment_t
{
    struct symbol_table_entry_t entries[NUM_ENTRIES_PER_FRAGMENT];
    struct symbol_table_fragment_t *next_fragment;
};

struct environment_t
{
    struct environment_t *parent_environment;
    struct symbol_table_fragment_t *symbol_table_fragment;
};

/*
 * create_global_environment
 */

struct environment_t *global_environment;

static void
create_environment(struct environment_t **environment_ptr)
{
    struct environment_t *environment = calloc(sizeof(struct environment_t), 1);
    *environment_ptr = environment;
}

static struct object_t **
get_bound_location(struct environment_t *environment, struct object_t *symbol, int recurse)
{
    struct environment_t *current_environment;
    struct symbol_table_fragment_t *fragment;
    int i;

    assert(symbol->tag_count.tag == TAG_SYMBOL);

    for (current_environment = environment; 
            current_environment != NULL; 
            current_environment = current_environment->parent_environment)
    {
        for (fragment = current_environment->symbol_table_fragment;
                fragment != NULL;
                fragment = fragment->next_fragment)
        {
            struct symbol_table_entry_t *entries = fragment->entries;

            for (i = 0; i < NUM_ENTRIES_PER_FRAGMENT; ++i)
            {
                if (entries[i].symbol 
                        && strcmp(entries[i].symbol->string_value, symbol->string_value) == 0)
                    return &entries[i].object;
            }
        }

        if (!recurse)
            return NULL;
    }

    return NULL;
}

static struct object_t **
bind(struct environment_t *environment, struct object_t *args)
{
    struct object_t **location;
    struct object_t *symbol;
    struct symbol_table_fragment_t *current_fragment;
    struct symbol_table_fragment_t *new_fragment;
    size_t i;

    assert(args->tag_count.tag == TAG_PAIR);
    symbol = CAR(args);

    assert(symbol->tag_count.tag == TAG_SYMBOL);
    location = get_bound_location(environment, symbol, 0);
    assert(location == NULL);

    current_fragment = environment->symbol_table_fragment;
    symbol = CAR(args);

    for (current_fragment = environment->symbol_table_fragment;
            current_fragment != NULL;
            current_fragment = current_fragment->next_fragment)
    {
        for (i = 0; i < NUM_ENTRIES_PER_FRAGMENT; ++i)
        {
            if (current_fragment->entries[i].symbol == NULL)
            {
                current_fragment->entries[i].symbol = symbol;
                return &current_fragment->entries[i].object;
            }
        }
    }

    new_fragment = calloc(sizeof(struct symbol_table_fragment_t), 1);
    new_fragment->next_fragment = environment->symbol_table_fragment;
    environment->symbol_table_fragment = new_fragment;
    new_fragment->entries[0].symbol = symbol;

    return &new_fragment->entries[0].object;
}

static struct object_t *
allocate_object(enum tag_t tag, size_t extra_size) 
{
    struct object_t *object = calloc(sizeof(struct object_t) + extra_size, 1);
    object->tag_count.tag = tag;

    return object;
}

static void
initialize_global_environment(struct environment_t *environment)
{
    struct special_function_initializer_t
    {
        const char *name;
        special_function_t function;
    };

    static struct special_function_initializer_t initializers[] = 
    {
        { "quote", quote },
        { "read", read },
        { "eval", eval },
        { "print", print },
        { "set!", set },
        { "cons", cons },
        { "car", car },
        { "cdr", cdr },
        { "define", define },
        { "lambda", lambda },
    };
    #define NUM_INITIALIZERS (sizeof initializers / sizeof initializers[0])
    size_t i;

    for (i = 0; i < NUM_INITIALIZERS; ++i)
    {
        size_t name_length = strlen(initializers[i].name);
        struct object_t *p0 = allocate_object(TAG_PAIR, 0);
        struct object_t *symbol = allocate_object(TAG_SYMBOL, name_length);
        struct object_t *function = allocate_object(TAG_SPECIAL_FUNCTION, 0);
        struct object_t **place;
        memmove(symbol->string_value, initializers[i].name, name_length);

        CAR(p0) = symbol;
        function->special_function_value = initializers[i].function;
        place = bind(environment, p0);
        *place = function;
    }
}


static int is_number(const struct token_t *input)
{
    if (input->text[0] >= '0' && input->text[0] <= '9')
        return 1;

    if ((input->text[0] == '-' || input->text[0] == '+')
            && (input->text[1] >= '0' && input->text[1] <= '9'))
        return 1;

    return 0;
}

static struct object_t *
object_from_symbol(const struct token_t *input)
{
    struct object_t *object;
    size_t string_length;

    if (input->text[0] == '#') 
    {
        /* #t/#f -> boolean */
        char next_char = tolower(input->text[1]);
        if (next_char == 't' || next_char == 'f')
        {
            object = allocate_object(TAG_BOOLEAN, 0);
            object->boolean_value = next_char == 't';
            return object;
        }

        /* #\blat -> char */
        assert(next_char == '\\');
        object = allocate_object(TAG_CHAR, 0);
        object->char_value = strstr(input->text, "newline") != NULL ? '\n' : input->text[2];
        return object;
    }
    else if (is_number(input)) 
    {
        if (strstr(input->text, ".") == NULL)
        {
            object = allocate_object(TAG_FIXNUM, 0);
            object->fixnum_value = strtol(input->text, NULL, 0);
        }
        else
        {
            object = allocate_object(TAG_FLONUM, 0);
            object->flonum_value = strtod(input->text, NULL);
        }
        return object;
    }

    /* If it's not a boolean, char, or number that we've parsed then it must 
       be a symbol or a string. Vectors, pairs, and procedures are allocated
       at runtime. */
    string_length = strlen(input->text);
    object = allocate_object(input->type == TOKEN_STRING ? TAG_STRING : TAG_SYMBOL, string_length);
    memmove(object->string_value, input->text, string_length);
    return object;
}

static struct object_t *
quote(struct environment_t *environment, struct object_t *args)
{
    UNUSED(environment);
    assert(args->tag_count.tag == TAG_PAIR);

    return CAR(args);
}

static struct object_t *
cons(struct environment_t *environment, struct object_t *args)
{
    struct object_t *object;

    UNUSED(environment);
    
    assert(args->tag_count.tag == TAG_PAIR);
    assert(CDR(args)->tag_count.tag == TAG_PAIR);

    object = allocate_object(TAG_PAIR, 0);

    CAR(object) = CAR(args);
    CDR(object) = CAR(CDR(args));

    return object;
}

static struct object_t *
car(struct environment_t *environment, struct object_t *args)
{
    UNUSED(environment);
    assert(args->tag_count.tag == TAG_PAIR);
    return CAR(args);
}

static struct object_t *
cdr(struct environment_t *environment, struct object_t *args)
{
    UNUSED(environment);
    assert(args->tag_count.tag == TAG_PAIR);
    return CDR(args);
}

static struct object_t empty_pair;

static struct object_t * 
recursive_create_object_from_token_stream(const struct token_t **input)
{
    if (*input == NULL)
        return &empty_pair;

    if ((*input)->type == TOKEN_LPAREN)
    {
        struct object_t *pair = allocate_object(TAG_PAIR, 0);
        *input = (const struct token_t *)(*input)->link.next;

        CAR(pair) = recursive_create_object_from_token_stream(input);

        if (*input)
        {
            CDR(pair) = recursive_create_object_from_token_stream(input);
            return pair;
        }
        else
        {
            return CAR(pair);
        }
    }
    else if ((*input)->type == TOKEN_RPAREN)
    {
        *input = (const struct token_t *)(*input)->link.next;
        return &empty_pair;
    }
    else
    {
        struct object_t *pair = allocate_object(TAG_PAIR, 0);
        CAR(pair) = object_from_symbol(*input);

        *input = (const struct token_t *)(*input)->link.next;
        CDR(pair) = recursive_create_object_from_token_stream(input);
        return pair;
    }
}

static struct object_t *
create_object_from_token_stream(const struct token_t *input)
{
    struct object_t *object = recursive_create_object_from_token_stream(&input);
    return object;
}

static struct object_t *
set(struct environment_t *environment, struct object_t *args)
{
    UNUSED(environment);
    assert(args->tag_count.tag == TAG_PAIR);
    assert(CDR(args)->tag_count.tag == TAG_PAIR);
    BREAK();
    return NULL;
}

static struct object_t *
define(struct environment_t *environment, struct object_t *args)
{
    /* fetch symbol binding location */
    /* evaluate argument */
    /* call set! on it */
    /* TODO: I think this incorrect, I don't think we need to evaluate place, it will
       either be a variable or a list of (variable lambda-list*) */
    struct object_t *place;
    struct object_t *value;

    assert(args->tag_count.tag == TAG_PAIR);
    assert(CDR(args)->tag_count.tag == TAG_PAIR);
    place = CAR(args);
    value = eval(environment, CDR(args));
    
    assert(place->tag_count.tag == TAG_SYMBOL || place->tag_count.tag == TAG_PAIR);
    if (place->tag_count.tag == TAG_SYMBOL)
    {
        struct object_t **location = bind(environment, args);
        *location = value;
    }
    else
    {
    }

    return value;
}

static struct object_t *
lambda(struct environment_t *environment, struct object_t *args)
{
    BREAK();
    return NULL;
}

/*
 * eval
 */

static struct object_t *
eval(struct environment_t *environment, struct object_t *args)
{
    struct object_t *first_arg;
    struct object_t **bound_location;
    assert(args->tag_count.tag == TAG_PAIR);

    first_arg = CAR(args);
    switch (first_arg->tag_count.tag)
    {
        case TAG_SPECIAL_FUNCTION:
            return first_arg->special_function_value(environment, CDR(args));
        case TAG_BOOLEAN:
        case TAG_CHAR:
        case TAG_VECTOR:
        case TAG_FIXNUM:
        case TAG_FLONUM:
        case TAG_PROCEDURE:
        case TAG_STRING:
            return first_arg;
        case TAG_SYMBOL:
            bound_location = get_bound_location(environment, first_arg, 1);
            assert(bound_location != NULL);
            return *bound_location;
        case TAG_PAIR:
            BREAK();
            break;
    }

    BREAK();
    return NULL;
}

/*
 * print
 */

static struct object_t *
print(struct environment_t *environment, struct object_t *args)
{
    UNUSED(environment);

    if (!args)
        return NULL;

    if (args == &empty_pair)
    {
        printf("'()");
        return NULL;
    }

    switch (args->tag_count.tag)
    {
        case TAG_BOOLEAN:
            printf("%s", args->boolean_value ? "#t" : "#f");
            return args + 1;
        case TAG_CHAR:
            printf("%c", args->char_value);
            return args + 1;
        case TAG_VECTOR:
            {
                int i;
                struct object_t *object = args + 1;

                printf("#(");
                for (i = 0; i < args->tag_count.tag; ++i)
                    object = print(environment, object);
                printf(")");
                return object;
            }
        case TAG_FIXNUM:
            printf("%ld", args->fixnum_value);
            return args + 1;
        case TAG_FLONUM:
            printf("%f", args->flonum_value);
            return args + 1;
        case TAG_PROCEDURE:
            printf("<procedure>");
            return args + 1;
        case TAG_STRING:
            printf("\"%s\"", args->string_value);
            return (struct object_t *)((char *)(args + 1) + strlen(args->string_value));
        case TAG_SYMBOL:
            printf("%s", args->string_value);
            return (struct object_t *)((char *)(args + 1) + strlen(args->string_value));
        case TAG_PAIR:
            print(environment, CAR(args));
            printf(" ");
            print(environment, CDR(args));
/*
            if (CDR(args) != &empty_pair && CDR(args) != NULL)
            {
                printf(" ");
                print(CDR(args));
            }

            printf(")");*/
            return args + 1;
        case TAG_SPECIAL_FUNCTION:
            printf("<special function>");
            return args + 1;
        default:
            assert(0);
            break;
    }

    return NULL;
}


struct object_t *
read(struct environment_t *environment, struct object_t *args)
{
    struct token_t *head;

    UNUSED(environment);
    assert(args->tag_count.tag == TAG_PAIR);

    head = tokenize(CAR(args)->string_value);
    return create_object_from_token_stream(head);
}


/*
 * main!
 */
int 
main(void) 
{
    const char *inputs[] = {
        "4",
        "(define count (lambda (item L) (if L (+ (equal? item (first L)) (count item (rest L))) 0)))",
        "(define fact (lambda (n) (if (<= n 1) 1 (* n (fact (- n 1))))))",
        "(define hello-world (lambda () (display \"hello world!\") (newline)))",
        "(define hello-world-2 (lambda (x) `(foo ,bar ,@(list 1 2 3))))",
        "(define vector-test #(1 2 3))"
    };
    size_t i;

    create_environment(&global_environment);
    initialize_global_environment(global_environment);

    for (i = 0; i < sizeof inputs / sizeof inputs[0]; ++i)
    {
        struct object_t *object;
        struct object_t *result;
        struct object_t *input_object;
        struct object_t *args;
        size_t input_length = strlen(inputs[i]);
    
        args = allocate_object(TAG_PAIR, 0);
        input_object = allocate_object(TAG_STRING, input_length);
        memmove(input_object->string_value, inputs[i], input_length);
        CAR(args) = input_object;

        object = read(global_environment, args);
        result = eval(global_environment, object);
        print(global_environment, result);
        printf("\n");
                
        /*while (head) 
        {
            printf("%s ", head->text);
            head = (struct token_t *)head->link.next;
        }
        */
    }

    return 0;
}

