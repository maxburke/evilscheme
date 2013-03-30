/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#ifndef EVIL_GC_H
#define EVIL_GC_H

#include "object.h"

struct evil_heap_t;

struct evil_heap_parameters_t;

struct evil_object_handle_t;

struct evil_object_handle_t *
evil_create_object_handle(struct evil_heap_t *heap, struct evil_object_t *object);

struct evil_object_handle_t *
evil_create_object_handle_from_value(struct evil_heap_t *heap, struct evil_object_t object);

void
evil_destroy_object_handle(struct evil_heap_t *heap, struct evil_object_handle_t *handle);

struct evil_object_t *
evil_resolve_object_handle(struct evil_object_handle_t *handle);

struct evil_heap_t *
gc_create(void *heap_mem, size_t heap_size);

void
gc_destroy(struct evil_heap_t *heap);

void
gc_set_environment(struct evil_heap_t *heap, struct evil_environment_t *environment);

struct evil_object_t *
gc_alloc(struct evil_heap_t *heap, enum evil_tag_t type, size_t extra_bytes);

struct evil_object_t *
gc_alloc_vector(struct evil_heap_t *heap, size_t count);

void
gc_collect(struct evil_heap_t *heap);

#endif
