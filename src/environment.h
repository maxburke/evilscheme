/***********************************************************************
 * evilscheme, Copyright (c) 2012-2015, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#ifndef EVIL_ENVIRONMENT_H
#define EVIL_ENVIRONMENT_H

#include "object.h"

#define NUM_ENTRIES_PER_FRAGMENT 16
#define INVALID_HASH 0

enum
{
    FIELD_SYMBOL_TABLE_FRAGMENT_NEXT_FRAGMENT,
    FIELD_SYMBOL_TABLE_FRAGMENT_NUM = (2 * NUM_ENTRIES_PER_FRAGMENT) + 1
};

#define SYMBOL_AT(fragment, i) VECTOR_BASE(fragment)[(2 * (i)) + 2]
#define OBJECT_AT(fragment, i) VECTOR_BASE(fragment)[(2 * (i)) + 1]

struct evil_environment_t;

struct evil_object_t *
get_bound_location_in_lexical_environment(struct evil_object_t *lexical_environment, uint64_t symbol_hash, int recurse);

struct evil_object_t *
get_bound_location(struct evil_environment_t *environment, uint64_t symbol_hash, int recurse);

struct evil_object_t *
bind(struct evil_environment_t *environment, struct evil_object_t lexical_environment, struct evil_object_t symbol);

#endif
