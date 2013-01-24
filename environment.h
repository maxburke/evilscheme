#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "object.h"

#define NUM_ENTRIES_PER_FRAGMENT 16

struct symbol_table_entry_t
{
    struct object_t symbol;
    struct object_t object;
};

struct symbol_table_fragment_t
{
    struct symbol_table_entry_t entries[NUM_ENTRIES_PER_FRAGMENT];
    struct symbol_table_fragment_t *next_fragment;
    size_t num_entries_in_fragment;
};

struct environment_t;

struct object_t *
get_bound_location(struct environment_t *environment, uint64_t symbol_hash, int recurse);

struct object_t *
bind(struct environment_t *environment, struct object_t *args);

#endif
