/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#ifndef EVIL_RUNTIME_H
#define EVIL_RUNTIME_H

#include "object.h"

struct heap_t;
struct symbol_table_fragment_t;

struct symbol_string_internment_page_t;
struct symbol_hash_internment_page_t;

struct interned_symbol_names_table_t
{
    struct symbol_hash_internment_page_t *hash_internment_page_base;
    struct symbol_string_internment_page_t *string_internment_page_base;
};

enum lexical_environment_fields_t
{
    FIELD_LEX_ENV_PARENT_ENVIRONMENT,
    FIELD_LEX_ENV_SYMBOL_TABLE_FRAGMENT,
    FIELD_LEX_ENV_NUM_FIELDS
};

struct evil_environment_t
{
    struct evil_tag_count_t tag_count;

    /*
     * These fields hold the saved VM state in case execution is interrupted
     * or postponed.
     */
    struct evil_object_t *stack_top;
    struct evil_object_t *stack_bottom;
    struct evil_object_t *stack_ptr;

    struct heap_t *heap;

    struct interned_symbol_names_table_t symbol_names;
    struct evil_object_t lexical_environment;
};

struct evil_environment_t *
evil_environment_create(void *stack, size_t stack_size, void *heap, size_t heap_size);

void
evil_environment_destroy(struct evil_environment_t *environment);

const char *
find_symbol_name(struct evil_environment_t *environment, uint64_t key);

uint64_t
register_symbol_from_string(struct evil_environment_t *environment, const char *string);

uint64_t
register_symbol_from_bytes(struct evil_environment_t *environment, const void *bytes, size_t num_bytes);

/*
 * C-environment interop functions
 */

#endif
