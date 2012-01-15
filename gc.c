#include "gc.h"

struct heap_t *
gc_create(void *heap_mem, size_t heap_size)
{
    struct heap_t *heap = malloc(sizeof(struct heap_t));
    heap->base = heap_mem;
    heap->top = (char *)heap_mem + heap_size;
    heap->ptr = heap_mem;
    heap->size = heap_size;

    return heap;
}

struct object_t *
gc_alloc(struct heap_t *heap, enum tag_t type, size_t extra_bytes)
{
    size_t total_alloc_size = (sizeof(struct object_t) + extra_bytes + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
    void *new_ptr = (char *)heap->ptr + total_alloc_size;
    struct object_t *object = heap->ptr;
    heap->ptr = new_ptr;

    if (type == TAG_STRING)
    {
        object->tag_count.count = extra_bytes;
    }
    else
    {
        assert(extra_bytes == 0);
        object->tag_count.count = 1;
    }

    object->tag_count.tag = type;
    return object;
}

void
gc_collect(struct environment_t *env)
{
}

