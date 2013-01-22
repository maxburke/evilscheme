#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "environment.h"
#include "object.h"
#include "builtins.h"
#include "read.h"
#include "runtime.h"

#define INVALID_HASH 0

struct object_t *
get_bound_location(struct environment_t *environment, struct object_t *args, int recurse)
{
    struct object_t *symbol;
    struct environment_t *current_environment;
    struct symbol_table_fragment_t *fragment;
    int i;
    uint64_t symbol_hash;

    symbol = deref(args);
    assert(symbol->tag_count.tag == TAG_SYMBOL);
    symbol_hash = symbol->value.symbol_hash;

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
                if (entries[i].symbol.value.symbol_hash == symbol_hash)
                    return &entries[i].object;
            }
        }

        if (!recurse)
            return NULL;
    }

    return NULL;
}

struct object_t *
bind(struct environment_t *environment, struct object_t *args)
{
    struct object_t *location;
    struct object_t *symbol;
    struct object_t *object;
    struct symbol_table_fragment_t *current_fragment;
    struct symbol_table_fragment_t *new_fragment;
    size_t i;

    object = deref(args);
    assert(object->tag_count.tag == TAG_PAIR);
    symbol = CAR(object);

    assert(symbol->tag_count.tag == TAG_SYMBOL);
    location = get_bound_location(environment, symbol, 0);
    if (location != NULL)
        return location;

    current_fragment = environment->symbol_table_fragment;
    symbol = deref(CAR(object));

    for (current_fragment = environment->symbol_table_fragment;
            current_fragment != NULL;
            current_fragment = current_fragment->next_fragment)
    {
        struct symbol_table_entry_t *entries = current_fragment->entries;

        for (i = 0; i < NUM_ENTRIES_PER_FRAGMENT; ++i)
        {
            if (entries[i].symbol.value.symbol_hash == INVALID_HASH)
            {
                entries[i].symbol = *symbol;
                return &entries[i].object;
            }
        }
    }

    new_fragment = calloc(sizeof(struct symbol_table_fragment_t), 1);
    new_fragment->next_fragment = environment->symbol_table_fragment;
    environment->symbol_table_fragment = new_fragment;
    new_fragment->entries[0].symbol = *symbol;

    return &new_fragment->entries[0].object;
}

