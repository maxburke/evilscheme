/***********************************************************************
 * evilscheme, Copyright (c) 2012-2015, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#ifndef EVIL_LINEAR_ALLOCATOR_H
#define EVIL_LINEAR_ALLOCATOR_H

struct linear_allocator_t;

struct linear_allocator_t *
linear_allocator_create(size_t reserve);

void
linear_allocator_destroy(struct linear_allocator_t *allocator);

void *
linear_allocator_alloc(struct linear_allocator_t *allocator, size_t size);

#endif

