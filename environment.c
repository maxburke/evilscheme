#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "environment.h"
#include "object.h"
#include "builtins.h"
#include "read.h"

struct environment_t *global_environment;

void
create_environment(struct environment_t **environment_ptr)
{
    struct environment_t *environment = calloc(sizeof(struct environment_t), 1);
    *environment_ptr = environment;
}

struct object_t **
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
                        && strcmp(entries[i].symbol->value.string_value, symbol->value.string_value) == 0)
                    return &entries[i].object;
            }
        }

        if (!recurse)
            return NULL;
    }

    return NULL;
}

struct object_t **
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

void
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
        memmove(symbol->value.string_value, initializers[i].name, name_length);

        CAR(p0) = symbol;
        function->value.special_function_value = initializers[i].function;
        place = bind(environment, p0);
        *place = function;
    }
}


