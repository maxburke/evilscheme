#include <stdlib.h>

#include "object.h"
    
struct object_t empty_pair_storage = { { TAG_PAIR, 0, 0 }, { 0 } };
struct object_t *empty_pair = &empty_pair_storage;

struct object_t *
allocate_object(enum tag_t tag, size_t extra_size) 
{
    struct object_t *object = calloc(sizeof(struct object_t) + extra_size, 1);
    object->tag_count.tag = tag;

    return object;
}


