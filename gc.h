/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license. 
 * See LICENSE.TXT for details.
 ***********************************************************************/

#ifndef GC_H
#define GC_H

#include "object.h"

struct heap_t
{
    struct tag_count_t tag_count;
    void *base;
    void *top;
    void *ptr;
    size_t size;
};

struct evil_object_handle_t
{
    struct object_t * volatile object;
    struct evil_object_handle_t *prev;
    struct evil_object_handle_t *next;
};

struct evil_object_handle_t
evil_create_object_handle(struct heap_t *heap, struct object_t *object);

struct evil_object_handle_t
evil_create_object_handle_from_value(struct heap_t *heap, struct object_t object);

void
evil_destroy_object_handle(struct heap_t *heap, struct evil_object_handle_t handle);

struct heap_t *
gc_create(void *heap_mem, size_t heap_size);

struct object_t *
gc_alloc(struct heap_t *heap, enum tag_t type, size_t extra_bytes);

struct object_t *
gc_alloc_vector(struct heap_t *heap, size_t count);

void
gc_collect(struct heap_t *heap);

#endif
