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
#include "dlist.h"
#include "environment.h"
#include "gc.h"
#include "runtime.h"
#include "slist.h"
#include "vm.h"

#define EVIL_DEFAULT_ALIGN 8
#define EVIL_DEFAULT_ALIGN_MASK (EVIL_DEFAULT_ALIGN - 1)

#ifndef EVIL_PAGE_SIZE
#define EVIL_PAGE_SIZE 4096
#endif

#define FLAG_MARKED 1
#define INITIAL_COST 0

struct evil_object_handle_t
{
    struct dlist_t link;
    struct evil_object_t * volatile object;
};

struct heap_bucket_t
{
    struct slist_t link;
    char *base;
    char *ptr;
    char *top;
};

struct bucket_cost_t
{
    struct heap_bucket_t *bucket;
    size_t cost;
};

struct heap_t
{
    struct evil_environment_t *environment;
    char *base;
    size_t size;

    unsigned char *card_base;
    size_t num_buckets;
    struct heap_bucket_t *bucket_base;
    struct heap_bucket_t *current_bucket;
    struct heap_bucket_t *free_list;
    struct bucket_cost_t *bucket_costs;

    struct dlist_t active_object_handles;
    struct dlist_t free_object_handles;
};

static struct heap_bucket_t *
acquire_bucket(struct heap_t *heap)
{
    struct heap_bucket_t *new_bucket;

    new_bucket = heap->free_list;
    if (new_bucket == NULL)
    {
        return NULL;
    }

    heap->free_list = (struct heap_bucket_t *)new_bucket->link.next;
    heap->current_bucket = new_bucket;

    return new_bucket;
}

static void *
perform_alloc(struct heap_t *heap, size_t size)
{
    struct heap_bucket_t *bucket;
    int i;
    
    for (i = 0; i < 2; ++i)
    {
        ptrdiff_t rounded_size;
        char *mem;

        bucket = heap->current_bucket;

        /*
         * TODO: We need to support large (>4kb) allocations sometime.
         */
        assert(size < EVIL_PAGE_SIZE);
        rounded_size = (ptrdiff_t)((size + EVIL_DEFAULT_ALIGN_MASK) & (~EVIL_DEFAULT_ALIGN_MASK));
        mem = bucket->ptr;

        if (bucket->top - mem >= rounded_size)
        {
            bucket->ptr = mem + rounded_size;
            memset(mem, 0, size);
            return mem;
        }

        bucket = acquire_bucket(heap);

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
create_buckets(struct heap_t *heap)
{
    size_t i;
    size_t num_buckets;
    struct heap_bucket_t *buckets;
    struct heap_bucket_t *current_bucket;
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

    current_bucket = acquire_bucket(heap);
    assert(current_bucket != NULL);
}

struct heap_t *
gc_create(void *heap_mem, size_t heap_size)
{
    struct heap_t *heap;
    size_t num_cards;
    size_t num_buckets;
    size_t bucket_alloc_size;
    void *bucket_base;
    size_t bucket_cost_size;

    heap = evil_aligned_alloc(sizeof(void *), sizeof(struct heap_t));
    memset(heap, 0, sizeof(struct heap_t));

    heap->base = heap_mem;
    heap->size = heap_size;

    num_cards = heap_size / (8 * EVIL_DEFAULT_ALIGN);
    heap->card_base = evil_aligned_alloc(sizeof(void *), num_cards);

    assert(heap_size % EVIL_PAGE_SIZE == 0);
    num_buckets = heap_size / EVIL_PAGE_SIZE;
    bucket_alloc_size = num_buckets * sizeof(struct heap_bucket_t);

    bucket_base = evil_aligned_alloc(sizeof(void *), bucket_alloc_size);
    memset(bucket_base, 0, bucket_alloc_size);
    heap->bucket_base = bucket_base;
    heap->num_buckets = num_buckets;

    bucket_cost_size = sizeof(struct bucket_cost_t) * num_buckets;
    heap->bucket_costs = evil_aligned_alloc(sizeof(void *), bucket_cost_size);

    dlist_initialize(&heap->active_object_handles);
    dlist_initialize(&heap->free_object_handles);

    create_buckets(heap);

    return heap;
}

void
gc_destroy(struct heap_t *heap)
{
    struct dlist_t *i;

    i = heap->active_object_handles.next;
    while (i != &heap->active_object_handles)
    {
        struct evil_object_handle_t *handle;

        handle = (struct evil_object_handle_t *)i;
        i = i->next;

        evil_aligned_free(handle);
    }

    i = heap->free_object_handles.next;
    while (i != &heap->free_object_handles)
    {
        struct evil_object_handle_t *handle;

        handle = (struct evil_object_handle_t *)i;
        i = i->next;
        evil_aligned_free(handle);
    }

    evil_aligned_free(heap->bucket_costs);
    evil_aligned_free(heap->bucket_base);
    evil_aligned_free(heap->card_base);
    evil_aligned_free(heap);
}

void
gc_set_environment(struct heap_t *heap, struct evil_environment_t *env)
{
    heap->environment = env;
}

struct evil_object_t *
gc_alloc(struct heap_t *heap, enum evil_tag_t type, size_t extra_bytes)
{
    size_t size;
    struct evil_object_t *object;

    if (type == TAG_PAIR)
    {
        object = gc_alloc_vector(heap, 2);
        object->tag_count.tag = TAG_PAIR;

        return object;
    }
    else
    {
        size = sizeof(struct evil_object_t) + extra_bytes;
    }

    object = perform_alloc(heap, size);

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
    else
    {
        object->tag_count.count = 1;
    }

    object->tag_count.tag = type;
    return object;
}

struct evil_object_t *
gc_alloc_vector(struct heap_t *heap, size_t count)
{
    /*
     * A vector is similar to an object but doesn't have the same contained data. It
     * has the tag_count header but following that the vector contains an array of simple
     * objects or references to complex objects (strings/symbols/functions/etc.)
     */
    size_t total_alloc_size;
    struct evil_object_t *object;

    total_alloc_size = (count * sizeof(struct evil_object_t)) + offsetof(struct evil_object_t, value);
    object = perform_alloc(heap, total_alloc_size);

    object->tag_count.tag = TAG_VECTOR;
    object->tag_count.count = (unsigned short)count;

    return object;
}

struct evil_object_handle_t *
evil_create_object_handle(struct evil_environment_t *environment, struct evil_object_t *object)
{
    struct heap_t *heap;
    struct evil_object_handle_t *handle;

    heap = environment->heap;
    handle = (struct evil_object_handle_t *)dlist_pop(&heap->free_object_handles);

    if (handle == NULL)
    {
        handle = evil_aligned_alloc(sizeof(void *), sizeof(struct evil_object_handle_t));
        memset(handle, 0, sizeof(struct evil_object_handle_t));
    }

    dlist_push(&heap->active_object_handles, &handle->link);

    handle->object = object;

    return handle;
}

struct evil_object_handle_t *
evil_create_object_handle_from_value(struct evil_environment_t *environment, struct evil_object_t object)
{
    unsigned char tag;

    tag = object.tag_count.tag;

    switch (tag)
    {
        case TAG_VECTOR:
        case TAG_PROCEDURE:
        case TAG_STRING:
        case TAG_PAIR:
            /*
             * We shouldn't see reference types here directly.
             */
            BREAK();
            break;
        case TAG_REFERENCE:
            return evil_create_object_handle(environment, object.value.ref);
        default:
            {
                struct evil_object_t *boxed_object;

                boxed_object = gc_alloc(environment->heap, tag, 0);
                *boxed_object = object;

                return evil_create_object_handle(environment, boxed_object);
            }
    }

    BREAK();
    return evil_create_object_handle(environment, NULL);
}

void
evil_destroy_object_handle(struct evil_environment_t *environment, struct evil_object_handle_t *handle)
{
    struct heap_t *heap;

    heap = environment->heap;
    handle->object = NULL;

    dlist_remove(&handle->link);
    dlist_push(&heap->free_object_handles, &handle->link);
}

struct evil_object_t *
evil_resolve_object_handle(struct evil_object_handle_t *handle)
{
    return handle->object;
}

void
evil_retarget_object_handle(struct evil_object_handle_t *handle, struct evil_object_t *object)
{
    handle->object = object;
}

static void
mark_object(struct heap_t *heap, struct evil_object_t *object)
{
    char *object_address;
    size_t object_offset;
    size_t bucket_index;
    struct bucket_cost_t *costs;

    object_address = (char *)object;
    object_offset = (size_t)object_address - (size_t)heap->base;

    object->tag_count.flag = FLAG_MARKED;

    /*
     * This check uses the overflow of unsigned arithmetic to check if an 
     * object resides in the heap. If the offset is creater than the heap size,
     * it is out of heap bounds and thus the bucket costs should not be 
     * updated.
     */

    if (object_offset > heap->size)
    {
        return;
    }

    costs = heap->bucket_costs;
    bucket_index = object_offset / EVIL_PAGE_SIZE;

    /*
     * The reasoning behind the costs is described below in 
     * initialize_bucket_costs
     */
    assert(costs[bucket_index].bucket == (heap->bucket_base + bucket_index));
    ++costs[bucket_index].cost;
}

static inline int
scan_object(struct heap_t *heap, struct evil_object_t *object)
{
    unsigned char tag;

    if (object == NULL)
    {
        return 0;
    }

    mark_object(heap, object);

    tag = object->tag_count.tag;

    switch (tag)
    {
        case TAG_BOOLEAN:
        case TAG_SYMBOL:
        case TAG_CHAR:
        case TAG_FIXNUM:
        case TAG_FLONUM:
        case TAG_EXTERNAL_FUNCTION:
        case TAG_STRING:
            return 0;

        case TAG_VECTOR:
        case TAG_PAIR:
        case TAG_PROCEDURE:
        case TAG_SPECIAL_FUNCTION:
            {
                unsigned short elements;
                unsigned short i;
                struct evil_object_t *base;

                elements = object->tag_count.count;
                base = VECTOR_BASE(object);

                for (i = 0; i < elements; ++i)
                {
                    scan_object(heap, base + i);
                }

                return 1;
            }
            break;

        case TAG_ENVIRONMENT:
            /*
             * TODO: Post-symbol-table-fragment refactoring, this will need
             * attention.
             */
            break;

        case TAG_REFERENCE:
            scan_object(heap, object->value.ref);
            return 0;

        case TAG_INNER_REFERENCE:
            {
                struct evil_object_t *parent;

                parent = object->value.ref;

                if (scan_object(heap, parent))
                {
                    struct evil_object_t *base;

                    base = VECTOR_BASE(parent);
                    scan_object(heap, base + object->tag_count.count);
                }
            }
            return 0;

        default:
            /*
             * Unhandled object type?
             */
            BREAK();
            break;
    }

    return 0;
}

static void
mark_evaluation_stack(struct heap_t *heap, struct evil_object_t *stack_ptr, struct evil_object_t *stack_top)
{
    struct evil_object_t *i;

    for (i = stack_ptr + 1; i < stack_top; ++i)
    {
        scan_object(heap, i);
    }
}

static void
mark_symbol_table(struct heap_t *heap, struct symbol_table_fragment_t *symbol_table)
{
    while (symbol_table != NULL)
    {
        int i;
        struct symbol_table_entry_t *entry;

        entry = symbol_table->entries;

        for (i = 0; i < NUM_ENTRIES_PER_FRAGMENT; ++i, ++entry)
        {
            assert(is_value_type(&entry->symbol));

            if (entry->symbol.value.symbol_hash != INVALID_HASH)
            {
                scan_object(heap, &entry->object);
            }
        }

        symbol_table = symbol_table->next_fragment;
    }
}

static void
mark_object_handles(struct heap_t *heap)
{
    struct dlist_t *i;

    for (i = heap->active_object_handles.next; i != &heap->active_object_handles; i = i->next)
    {
        struct evil_object_handle_t *handle;

        handle = (struct evil_object_handle_t *)i;
        scan_object(heap, handle->object);
    }
}

static void
mark_roots(struct heap_t *heap, struct evil_environment_t *environment)
{
    mark_evaluation_stack(heap, environment->stack_ptr, environment->stack_top);
    mark_symbol_table(heap, environment->symbol_table_fragment);
    mark_object_handles(heap);
}

static void
initialize_bucket_costs(struct heap_t *heap)
{
    size_t i;
    size_t num_buckets;
    struct heap_bucket_t *bucket_base;
    struct bucket_cost_t *costs;

    /*
     * Bucket costs are determined by the number of in-references to all 
     * objects in the bucket. More expensive buckets will have more live
     * objects and more references into the bucket.
     *
     * That said, all references across the whole system will have to be 
     * updated if *any* bucket is compacted so it doesn't do a lot to alleviate
     * that pain. It does however allow us to see which buckets are definitely
     * empty and which buckets may cost a lot to compact.
     */

    bucket_base = heap->bucket_base;
    costs = heap->bucket_costs;
    for (i = 0, num_buckets = heap->num_buckets; i < num_buckets; ++i)
    {
        /*
         * The bucket pointer is inserted into the cost structure so that we
         * can know which bucket we are referring to after the cost array is
         * sorted.
         */
        costs[i].bucket = bucket_base + i;

        /*
         * Initial cost is set to zero so that if a bucket is not referenced
         * at all then we can assume it is completely dead and can be trivially
         * reclaimed.
         */
        costs[i].cost = INITIAL_COST;
    }
}

static void
reclaim_bucket(struct heap_t *heap, struct heap_bucket_t *bucket)
{
    bucket->ptr = bucket->base;
    bucket->link.next = &heap->free_list->link;
    heap->free_list = bucket;
}

static size_t
reclaim_empty_buckets(struct heap_t *heap)
{
    size_t i;
    size_t num_buckets;
    size_t num_reclaimed;
    struct bucket_cost_t *costs;

    num_reclaimed = 0;
    costs = heap->bucket_costs;
    for (i = 0, num_buckets = heap->num_buckets; i < num_buckets; ++i)
    {
        struct heap_bucket_t *bucket;

        bucket = costs[i].bucket;
        if (costs[i].cost == INITIAL_COST)
        {
            reclaim_bucket(heap, bucket);
            ++num_reclaimed;
        }
    }

    return num_reclaimed;
}

void
gc_collect(struct heap_t *heap)
{
    struct evil_environment_t *environment;
    size_t num_reclaimed;

    environment = heap->environment;
    assert(environment != NULL);

    initialize_bucket_costs(heap);
    mark_roots(heap, environment);

    /*
     * Pick through all the buckets that are empty and reclaim them.
     */

    num_reclaimed = reclaim_empty_buckets(heap);

    if (num_reclaimed > 0)
    {
        /*
         * It's entirely possible that we reclaim the bucket the garbage
         * collector is currently allocating. If this happens, we should 
         * acquire a new one so that the next allocation succeeds. If the 
         * current bucket is not reclaimed, it does not really hurt to acquire
         * a new one as the GC will probably do this on its own anyways if
         * the collection was triggered by a failure to allocate in the bucket.
         */
        heap->current_bucket = acquire_bucket(heap);
        return;
    }

    /*
     * If there are no buckets reclaimed then we sort the bucket cost list and
     * start compacting the cheapest buckets. There is one problem -- we need
     * at least one bucket to compact into. I'll add that as a TODO.
     */

    BREAK();
}


