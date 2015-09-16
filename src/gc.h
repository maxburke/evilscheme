/***********************************************************************
 * evilscheme, Copyright (c) 2012-2015, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#ifndef EVIL_GC_H
#define EVIL_GC_H

#include "object.h"

struct heap_t;

struct heap_parameters_t;

struct heap_t *
gc_create(void *heap_mem, size_t heap_size);

void
gc_destroy(struct heap_t *heap);

void
gc_set_environment(struct heap_t *heap, struct evil_environment_t *environment);

struct evil_object_t *
gc_alloc(struct heap_t *heap, enum evil_tag_t type, size_t extra_bytes);

struct evil_object_t *
gc_alloc_vector(struct heap_t *heap, size_t count);

void
gc_collect(struct heap_t *heap);

#endif
