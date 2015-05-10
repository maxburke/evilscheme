/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "environment.h"
#include "object.h"
#include "evil_scheme.h"
#include "runtime.h"

struct evil_object_t *
get_bound_location(struct evil_environment_t *environment, uint64_t symbol_hash, int recurse)
{
    struct evil_environment_t *current_environment;
    struct symbol_table_fragment_t *fragment;
    int i;

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
                {
                    return &entries[i].object;
                }
            }
        }

        if (!recurse)
            return empty_pair;
    }

    return empty_pair;
}

struct evil_object_t *
bind(struct evil_environment_t *environment, struct evil_object_t symbol)
{
    struct evil_object_t *location;
    struct symbol_table_fragment_t *current_fragment;
    struct symbol_table_fragment_t *new_fragment;
    size_t i;

    assert(symbol.tag_count.tag == TAG_SYMBOL);
    location = get_bound_location(environment, symbol.value.symbol_hash, 0);
    if (location != empty_pair)
        return location;

    for (current_fragment = environment->symbol_table_fragment;
            current_fragment != NULL;
            current_fragment = current_fragment->next_fragment)
    {
        struct symbol_table_entry_t *entries = current_fragment->entries;

        for (i = 0; i < NUM_ENTRIES_PER_FRAGMENT; ++i)
        {
            if (entries[i].symbol.value.symbol_hash == INVALID_HASH)
            {
                entries[i].symbol = symbol;
                return &entries[i].object;
            }
        }
    }

    new_fragment = calloc(sizeof(struct symbol_table_fragment_t), 1);
    new_fragment->next_fragment = environment->symbol_table_fragment;
    environment->symbol_table_fragment = new_fragment;
    new_fragment->entries[0].symbol = symbol;

    return &new_fragment->entries[0].object;
}

