/***********************************************************************
 * evilscheme, Copyright (c) 2012-2015, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#include <string.h>

#include "evil_scheme.h"
#include "linear_allocator.h"

#ifndef LINEAR_ALLOCATOR_PAGE_SIZE
#define LINEAR_ALLOCATOR_PAGE_SIZE 4096
#endif

#ifndef LINEAR_ALLOCATOR_DEFAULT_ALIGNMENT
#define LINEAR_ALLOCATOR_DEFAULT_ALIGNMENT sizeof(void *)
#endif

struct linear_allocator_chunk_t
{
    struct linear_allocator_chunk_t *next;
    char *top;
    char *end;
    char mem[1];
};

struct linear_allocator_t
{
    struct linear_allocator_chunk_t *head;
};

static struct linear_allocator_chunk_t *
linear_allocator_create_chunk(size_t reserve)
{
    size_t initial_size;
    struct linear_allocator_chunk_t *ptr;

    initial_size = (sizeof(struct linear_allocator_chunk_t) + reserve + (LINEAR_ALLOCATOR_PAGE_SIZE - 1)) & ~(LINEAR_ALLOCATOR_PAGE_SIZE - 1);
    ptr = evil_aligned_alloc(LINEAR_ALLOCATOR_PAGE_SIZE, initial_size);

    ptr->next = NULL;
    ptr->top = ptr->mem;
    ptr->end = ptr->top + reserve;

    return ptr;
}

struct linear_allocator_t *
linear_allocator_create(size_t reserve)
{
    struct linear_allocator_t *allocator;

    allocator = evil_aligned_alloc(sizeof(void *), sizeof(struct linear_allocator_t));
    allocator->head = linear_allocator_create_chunk(reserve);

    return allocator;
}

static void
discard_allocator_chunk(struct linear_allocator_chunk_t *chunk)
{
    if (chunk != NULL)
    {
        discard_allocator_chunk(chunk->next);
        evil_aligned_free(chunk);
    }
}

void
linear_allocator_destroy(struct linear_allocator_t *allocator)
{
    discard_allocator_chunk(allocator->head);
    evil_aligned_free(allocator);
}

void *
linear_allocator_alloc(struct linear_allocator_t *allocator, size_t size)
{
    char *top;
    char *end;
    size_t rounded_size;
    struct linear_allocator_chunk_t *chunk;

    chunk = allocator->head;
    top = chunk->top;
    end = chunk->end;
    rounded_size = (size + LINEAR_ALLOCATOR_DEFAULT_ALIGNMENT - 1) & ~(LINEAR_ALLOCATOR_DEFAULT_ALIGNMENT - 1);

    if ((size_t)(end - top) < rounded_size)
    {
        chunk = linear_allocator_create_chunk(rounded_size);
        chunk->next = allocator->head;
        allocator->head = chunk;

        return linear_allocator_alloc(allocator, size);
    }

    chunk->top = top + rounded_size;
    memset(top, 0, rounded_size);

    return top;
}

