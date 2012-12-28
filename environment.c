#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "environment.h"
#include "object.h"
#include "builtins.h"
#include "read.h"
#include "runtime.h"

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
                        && entries[i].symbol->value.symbol_hash == symbol->value.symbol_hash)
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

