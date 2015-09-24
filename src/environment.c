/***********************************************************************
 * evilscheme, Copyright (c) 2012-2015, Maximilian Burke
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
#include "gc.h"

struct evil_object_t *
get_bound_location_in_lexical_environment(struct evil_object_t *lexical_environment, uint64_t symbol_hash, int recurse)
{
    struct evil_object_t *lexical_environment_ptr;

    for (lexical_environment_ptr = deref(lexical_environment);
            lexical_environment_ptr != empty_pair;
            lexical_environment_ptr = deref(&VECTOR_BASE(lexical_environment_ptr)[FIELD_LEX_ENV_PARENT_ENVIRONMENT]))
    {
        struct evil_object_t *symbol_table_fragment;

        for (symbol_table_fragment = deref(&VECTOR_BASE(lexical_environment_ptr)[FIELD_LEX_ENV_SYMBOL_TABLE_FRAGMENT]);
                symbol_table_fragment != empty_pair;
                symbol_table_fragment = deref(&VECTOR_BASE(symbol_table_fragment)[FIELD_SYMBOL_TABLE_FRAGMENT_NEXT_FRAGMENT]))
        {
            int i;

            for (i = 0; i < NUM_ENTRIES_PER_FRAGMENT; ++i)
            {
                const uint64_t hash = SYMBOL_AT(symbol_table_fragment, i).value.symbol_hash;

                if (hash == INVALID_HASH)
                {
                    break;
                }

                if (hash == symbol_hash)
                {
                    /*
                     * TODO: Return an inner reference.
                     */
                    return &OBJECT_AT(symbol_table_fragment, i);
                }
            }
        }

        if (!recurse)
        {
            return empty_pair;
        }
    }

    return empty_pair;
}

struct evil_object_t *
get_bound_location(struct evil_environment_t *environment, uint64_t symbol_hash, int recurse)
{
    return get_bound_location_in_lexical_environment(&environment->lexical_environment, symbol_hash, recurse);
}

static void
append_symbol_table_fragment(struct evil_object_t *symbol_table_fragment, struct evil_object_t *new_fragment)
{
    for (;;)
    {
        struct evil_object_t *next_symbol_table_fragment;

        next_symbol_table_fragment = deref(&VECTOR_BASE(symbol_table_fragment)[FIELD_SYMBOL_TABLE_FRAGMENT_NEXT_FRAGMENT]);

        if (next_symbol_table_fragment == empty_pair)
        {
            VECTOR_BASE(symbol_table_fragment)[FIELD_SYMBOL_TABLE_FRAGMENT_NEXT_FRAGMENT] = make_ref(new_fragment);

            return;
        }
        else
        {
            symbol_table_fragment = next_symbol_table_fragment;
        }
    }
}

/*
 * bind needs to take a lexical environment, not environment.
 */
struct evil_object_t *
bind(struct evil_environment_t *environment, struct evil_object_t lexical_environment, struct evil_object_t symbol)
{
    struct evil_object_t *lexical_environment_ptr;
    uint64_t symbol_hash;
    struct evil_object_t *location;
    struct evil_object_t *symbol_table_fragment;
    struct evil_object_t *new_fragment;
    int i;
    struct evil_object_t default_symbol_value;
    struct evil_object_handle_t *handle;

    assert(symbol.tag_count.tag == TAG_SYMBOL);

    lexical_environment_ptr = deref(&lexical_environment);
    symbol_hash = symbol.value.symbol_hash;
    location = get_bound_location_in_lexical_environment(lexical_environment_ptr, symbol_hash, 0);

    if (location != empty_pair)
    {
        return location;
    }

    for (symbol_table_fragment = deref(&VECTOR_BASE(lexical_environment_ptr)[FIELD_LEX_ENV_SYMBOL_TABLE_FRAGMENT]);
            symbol_table_fragment != empty_pair;
            symbol_table_fragment = deref(&VECTOR_BASE(symbol_table_fragment)[FIELD_SYMBOL_TABLE_FRAGMENT_NEXT_FRAGMENT]))
    {
        for (i = 0; i < NUM_ENTRIES_PER_FRAGMENT; ++i)
        {
            struct evil_object_t *symbol_ptr;

            symbol_ptr = &SYMBOL_AT(symbol_table_fragment, i);
            if (symbol_ptr->value.symbol_hash == INVALID_HASH)
            {
                *symbol_ptr = symbol;

                /*
                 * TODO: Return an inner reference.
                 */
                return &OBJECT_AT(symbol_table_fragment, i);
            }
        }
    }

    handle = evil_create_object_handle(environment, lexical_environment_ptr);

    new_fragment = gc_alloc_vector(environment->heap, FIELD_SYMBOL_TABLE_FRAGMENT_NUM);

    lexical_environment_ptr = evil_resolve_object_handle(handle);
    evil_destroy_object_handle(environment, handle);

    /*
     * New fragments are placed at the end of the linked fragment list.
     * Initial implementation had them at the beginning but:
     *   a) this is slow for partially filled fragments
     *   b) it made it difficult to link to the root of an environment.
     */

    symbol_table_fragment = deref(&VECTOR_BASE(lexical_environment_ptr)[FIELD_LEX_ENV_SYMBOL_TABLE_FRAGMENT]);

    if (symbol_table_fragment == empty_pair)
    {
        VECTOR_BASE(lexical_environment_ptr)[FIELD_LEX_ENV_SYMBOL_TABLE_FRAGMENT] = make_ref(new_fragment);
    }
    else
    {
        append_symbol_table_fragment(symbol_table_fragment, new_fragment);
    }

    VECTOR_BASE(new_fragment)[FIELD_SYMBOL_TABLE_FRAGMENT_NEXT_FRAGMENT] = make_empty_ref();

    default_symbol_value.tag_count.tag = TAG_SYMBOL;
    default_symbol_value.tag_count.flag = 0;
    default_symbol_value.tag_count.count = 1;
    default_symbol_value.value.symbol_hash = 0;

    for (i = 1; i < NUM_ENTRIES_PER_FRAGMENT; ++i)
    {
        SYMBOL_AT(new_fragment, i) = default_symbol_value;
        OBJECT_AT(new_fragment, i) = make_ref(empty_pair);
    }

    SYMBOL_AT(new_fragment, 0) = symbol;
    OBJECT_AT(new_fragment, 0) = make_ref(empty_pair);

    /*
     * TODO: Return an inner reference.
     */
    return &OBJECT_AT(new_fragment, 0);
}

