/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "gc.h"
#include "runtime.h"
#include "slist.h"
#include "vm.h"

#define EVIL_DEFAULT_ALIGN 8
#define EVIL_DEFAULT_ALIGN_MASK (EVIL_DEFAULT_ALIGN - 1)

#ifndef EVIL_PAGE_SIZE
#define EVIL_PAGE_SIZE 4096
#endif

struct evil_heap_bucket_t
{
    struct slist_t link;
    char *base;
    char *ptr;
    char *top;
};

struct evil_heap_t
{
    char *base;
    size_t size;

    unsigned char *card_base;
    size_t num_buckets;
    struct evil_heap_bucket_t *bucket_base;
    struct evil_heap_bucket_t *current_bucket;
    struct evil_heap_bucket_t *free_list;
    struct evil_heap_bucket_t *used_list;
};

static struct evil_heap_bucket_t *
evil_gc_acquire_bucket(struct evil_heap_t *heap)
{
    struct evil_heap_bucket_t *new_bucket;
    struct evil_heap_bucket_t *current_bucket;

    new_bucket = heap->free_list;
    if (new_bucket == NULL)
    {
        return NULL;
    }

    heap->free_list = (struct evil_heap_bucket_t *)new_bucket->link.next;

    current_bucket = heap->current_bucket;
    if (current_bucket != NULL)
    {
        current_bucket->link.next = &heap->used_list->link;
        heap->used_list = current_bucket;
    }

    heap->current_bucket = new_bucket;

    return new_bucket;
}

static void *
evil_gc_perform_alloc(struct evil_heap_t *heap, size_t size)
{
    struct evil_heap_bucket_t *bucket;
    int i;

    bucket = heap->current_bucket;
    for (i = 0; i < 2; ++i)
    {
        ptrdiff_t rounded_size;
        char *mem;

        /*
         * TODO: We need to support large (>4kb) allocations sometime.
         */
        assert(size < EVIL_PAGE_SIZE);
        rounded_size = (ptrdiff_t)((size + EVIL_DEFAULT_ALIGN_MASK) & (~EVIL_DEFAULT_ALIGN_MASK));
        mem = bucket->ptr;

        if (bucket->top - mem >= rounded_size)
        {
            bucket->ptr = mem + rounded_size;
            return mem;
        }

        bucket = evil_gc_acquire_bucket(heap);

        if (bucket == NULL)
        {
            gc_collect(heap);
        }
    }

    /*
     * If we're at this point then a garbage collection could not solve our
     * woes. Perhaps we need a bigger heap? Perhaps we need to collect
     * again? (probably not, there are no finalizers to run so it's not like
     * collecting again will rid the heap of newly dead refs.) Who knows!
     *
     * Woe :(
     */
    BREAK();

    return NULL;
}

static void
evil_gc_create_buckets(struct evil_heap_t *heap)
{
    size_t i;
    size_t num_buckets;
    struct evil_heap_bucket_t *buckets;
    struct evil_heap_bucket_t *current_bucket;
    char *heap_base;

    buckets = heap->bucket_base;
    heap_base = heap->base;
    for (i = 0, num_buckets = heap->num_buckets; i < num_buckets; ++i)
    {
        char *bucket_base;

        bucket_base = heap_base + i * EVIL_PAGE_SIZE;
        buckets[i].base = bucket_base;
        buckets[i].ptr = bucket_base;
        buckets[i].top = bucket_base + EVIL_PAGE_SIZE;

        buckets[i].link.next = &heap->free_list->link;
        heap->free_list = &buckets[i];
    }

    current_bucket = evil_gc_acquire_bucket(heap);
    assert(current_bucket != NULL);
}

struct evil_heap_t *
gc_create(void *heap_mem, size_t heap_size)
{
    struct evil_heap_t *heap;
    size_t num_cards;

    heap = evil_aligned_alloc(sizeof(void *), sizeof(struct evil_heap_t));
    memset(heap, 0, sizeof(struct evil_heap_t));

    heap->base = heap_mem;
    heap->size = heap_size;

    num_cards = heap_size / (8 * EVIL_DEFAULT_ALIGN);
    heap->card_base = evil_aligned_alloc(sizeof(void *), num_cards);

    heap->num_buckets = heap_size / EVIL_PAGE_SIZE;
    assert(heap_size % EVIL_PAGE_SIZE == 0);
    heap->bucket_base = evil_aligned_alloc(sizeof(void *), heap->num_buckets * sizeof(struct evil_heap_bucket_t));

    heap->free_list = NULL;
    heap->used_list = NULL;
    heap->current_bucket = NULL;

    evil_gc_create_buckets(heap);

    return heap;
}

struct object_t *
gc_alloc(struct evil_heap_t *heap, enum tag_t type, size_t extra_bytes)
{
    size_t size;
    struct object_t *object;

    if (type == TAG_ENVIRONMENT)
    {
        size = sizeof(struct environment_t);
    }
    else if (type == TAG_PAIR)
    {
        size = offsetof(struct object_t, value) + 2 * sizeof(struct object_t);
    }
    else
    {
        size = sizeof(struct object_t) + extra_bytes;
    }

    object = evil_gc_perform_alloc(heap, size);

    /*
     * Vectors (and procedures) need to be allocated through gc_alloc_vector
     * below.
     */
    assert(type != TAG_VECTOR && type != TAG_PROCEDURE && type != TAG_SPECIAL_FUNCTION);

    if (type == TAG_STRING)
    {
        assert(extra_bytes < 65536);
        object->tag_count.count = (unsigned short)extra_bytes;
    }
    else if (type == TAG_PAIR)
    {
        /*
         * TODO: pairs should probably go through gc_alloc_vector below.
         */
        object->tag_count.count = 2;
    }
    else
    {
        object->tag_count.count = 1;
    }

    object->tag_count.tag = type;
    return object;
}

struct object_t *
gc_alloc_vector(struct evil_heap_t *heap, size_t count)
{
    /*
     * A vector is similar to an object but doesn't have the same contained data. It
     * has the tag_count header but following that the vector contains an array of simple
     * objects or references to complex objects (strings/symbols/functions/etc.)
     */
    size_t total_alloc_size;
    struct object_t *object;

    total_alloc_size = (count * sizeof(struct object_t)) + offsetof(struct object_t, value);
    object = evil_gc_perform_alloc(heap, total_alloc_size);

    object->tag_count.tag = TAG_VECTOR;
    object->tag_count.count = (unsigned short)count;

    return object;
}

void
gc_collect(struct evil_heap_t *env)
{
    UNUSED(env);
    BREAK();
}

struct evil_object_handle_t
evil_create_object_handle(struct evil_heap_t *heap, struct object_t *object)
{
    struct evil_object_handle_t handle;

    /*
     * TODO: This needs to link object handles together so that we can iterate
     * over them during garbage collection.
     */

    UNUSED(heap);

    handle.object = object;
    handle.prev = NULL;
    handle.next = NULL;

    return handle;
}

struct evil_object_handle_t
evil_create_object_handle_from_value(struct evil_heap_t *heap, struct object_t object)
{
    unsigned char tag;

    tag = object.tag_count.tag;
    switch (tag)
    {
        case TAG_VECTOR:
        case TAG_PROCEDURE:
        case TAG_STRING:
        case TAG_PAIR:
        case TAG_ENVIRONMENT:
            /*
             * We shouldn't see reference types here directly.
             */
            BREAK();
            break;
        case TAG_REFERENCE:
            return evil_create_object_handle(heap, object.value.ref);
        default:
            {
                struct object_t *boxed_object;

                boxed_object = gc_alloc(heap, tag, 0);
                *boxed_object = object;

                return evil_create_object_handle(heap, boxed_object);
            }
    }

    BREAK();
    return evil_create_object_handle(heap, NULL);
}

void
evil_destroy_object_handle(struct evil_heap_t *heap, struct evil_object_handle_t handle)
{
    /*
     * TODO: This needs to un-link object handles from the master list.
     */

    UNUSED(heap);
    UNUSED(handle);
}

