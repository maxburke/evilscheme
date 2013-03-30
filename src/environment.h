/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#ifndef EVIL_ENVIRONMENT_H
#define EVIL_ENVIRONMENT_H

#include "object.h"

#define NUM_ENTRIES_PER_FRAGMENT 16
#define INVALID_HASH 0

struct symbol_table_entry_t
{
    struct evil_object_t symbol;
    struct evil_object_t object;
};

struct symbol_table_fragment_t
{
    struct symbol_table_entry_t entries[NUM_ENTRIES_PER_FRAGMENT];
    struct symbol_table_fragment_t *next_fragment;
};

struct evil_environment_t;

struct evil_object_t *
get_bound_location(struct evil_environment_t *environment, uint64_t symbol_hash, int recurse);

struct evil_object_t *
bind(struct evil_environment_t *environment, struct evil_object_t symbol);

#endif