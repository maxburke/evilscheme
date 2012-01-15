#ifndef GC_H
#define GC_H

struct heap_t
{
    void *base;
    void *top;
    void *ptr;
    size_t size;
};

struct heap_t *
gc_create(void *heap_mem, size_t heap_size);

struct object_t *
gc_alloc(struct heap_t *heap, enum tag_t type, size_t extra_bytes);

void
gc_collect(struct environment_t *env);

#endif
