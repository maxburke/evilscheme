/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#ifndef RUNTUME_H
#define RUNTIME_H

#include "object.h"

struct evil_heap_t;
struct symbol_table_fragment_t;

struct symbol_string_internment_page_t;
struct symbol_hash_internment_page_t;

struct interned_symbol_names_table_t
{
    struct symbol_hash_internment_page_t *hash_internment_page_base;
    struct symbol_string_internment_page_t *string_internment_page_base;
};

struct environment_t
{
    struct tag_count_t tag_count;

    /*
     * These fields hold the saved VM state in case execution is interrupted
     * or postponed.
     */
    struct object_t *stack_top;
    struct object_t *stack_bottom;
    struct object_t *stack_ptr;

    struct evil_heap_t *heap;
    struct environment_t *parent_environment;
    struct symbol_table_fragment_t *symbol_table_fragment;
    struct interned_symbol_names_table_t symbol_names;
};

struct environment_t *
evil_environment_create(void *stack, size_t stack_size, void *heap, size_t heap_size);

const char *
find_symbol_name(struct environment_t *environment, uint64_t key);

uint64_t
register_symbol_from_string(struct environment_t *environment, const char *string);

uint64_t
register_symbol_from_bytes(struct environment_t *environment, const void *bytes, size_t num_bytes);

/*
 * C-environment interop functions
 */

#endif
