#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "base.h"
#include "runtime.h"
#include "gc.h"

#define DEFAULT_ALIGN sizeof(void *)
#define DEFAULT_ALIGN_MASK (DEFAULT_ALIGN - 1)

static void *
gc_perform_alloc(struct heap_t *heap, size_t size)
{
    void *mem;
    size_t aligned_size;

    mem = heap->ptr;
    aligned_size = (size + DEFAULT_ALIGN_MASK) & ~(DEFAULT_ALIGN_MASK);

    if ((char *)heap->ptr + size >= (char *)heap->top)
        gc_collect(heap);

    assert((char *)heap->ptr + size < (char *)heap->top);
    heap->ptr = (char *)mem + aligned_size;
    memset(mem, 0, aligned_size);

    return mem;
}

struct heap_t *
gc_create(void *heap_mem, size_t heap_size)
{
    struct heap_t *heap;
    
    heap = heap_mem;
    memset(heap, 0, sizeof(struct heap_t));

    heap->tag_count.tag = TAG_HEAP;
    heap->tag_count.count = 1;
    heap->base = heap_mem;
    heap->top = (char *)heap_mem + heap_size;
    heap->ptr = (char *)heap_mem + sizeof(struct heap_t);
    heap->size = heap_size;

    return heap;
}

struct object_t *
gc_alloc(struct heap_t *heap, enum tag_t type, size_t extra_bytes)
{
    size_t size;
    struct object_t *object;

    size = (type != TAG_ENVIRONMENT) ? sizeof(struct object_t) + extra_bytes : sizeof(struct environment_t);
    object = gc_perform_alloc(heap, size);

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

struct object_t *
gc_alloc_vector(struct heap_t *heap, size_t count)
{
    /*
     * A vector is similar to an object but doesn't have the same contained data. It 
     * has the tag_count header but following that the vector contains an array of simple
     * objects or references to complex objects (strings/symbols/functions/etc.)
     */
    size_t total_alloc_size; 
    struct object_t *object;

    total_alloc_size = (count * sizeof(struct object_t)) + sizeof(struct tag_count_t);
    object = gc_perform_alloc(heap, total_alloc_size);

    object->tag_count.tag = TAG_VECTOR;
    object->tag_count.count = (unsigned short)count;

    return object;
}

void
gc_collect(struct heap_t *env)
{
    UNUSED(env);
    BREAK();
}

