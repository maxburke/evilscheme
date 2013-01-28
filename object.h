/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license. 
 * See LICENSE.TXT for details.
 ***********************************************************************/

#ifndef OBJECT_H
#define OBJECT_H

#include <stddef.h>
#include <stdint.h>
#include "base.h"

enum tag_t
{
    TAG_INVALID,
    TAG_BOOLEAN,
    TAG_SYMBOL,
    TAG_CHAR,
    TAG_VECTOR,
    TAG_PAIR,
    TAG_FIXNUM,
    TAG_FLONUM,
    TAG_STRING,
    TAG_PROCEDURE,

    /*
     * Special functions include read, eval, print, define. These functions 
     * are part of the core but must also be accessible and usable from within
     * the script.
     */
    TAG_SPECIAL_FUNCTION,

    /*
     * The environment and heap are both given object tags as they will both
     * be traversed during garbage collection.
     */
    TAG_ENVIRONMENT,
    TAG_HEAP,

    /*
     * References are how aggregate objects are handled within the system
     * in most cases. For example, a vector will exist on the heap but 
     * it is a reference to it that will be stored in a pair, or on the stack,
     * or within another vector.
     */
    TAG_REFERENCE,

    /*
     * Inner references are a type of reference to the inside of an aggregate
     * object like a vector or string and are comparable to .NET's managed
     * pointers. Typically these are created with functions like vector-ref,
     * string-ref, vector-set!, string-set!
     */
    TAG_INNER_REFERENCE
};

struct tag_count_t
{
    unsigned char tag;
    unsigned char flag;
    unsigned short count;
};

struct object_t;
struct environment_t;
typedef struct object_t (*special_function_t)(struct environment_t *, struct object_t *);

union object_value_t
{
    int64_t fixnum_value;
    double flonum_value;
    uint64_t symbol_hash;
    special_function_t special_function_value;
    char string_value[1];
    struct object_t *ref;
};

struct object_t
{
    struct tag_count_t tag_count;
    union object_value_t value;
};

extern struct object_t *empty_pair;

const char *
type_name(enum tag_t tag);

#define VECTOR_BASE(x) ((struct object_t *)(&(x)->value))
#define RAW_CAR(x) (VECTOR_BASE(x))
#define RAW_CDR(x) (VECTOR_BASE(x) + 1)
#define CAR(x) deref((VECTOR_BASE(x)))
#define CDR(x) deref((VECTOR_BASE(x) + 1))

static inline struct object_t
make_ref(struct object_t *ptr)
{
    struct object_t ref;
    unsigned char tag;

    tag = ptr->tag_count.tag;

    if (tag == TAG_PAIR || tag == TAG_VECTOR || tag == TAG_PROCEDURE || tag == TAG_STRING)
    {
        ref.tag_count.tag = TAG_REFERENCE;
        ref.tag_count.flag = 0;
        ref.tag_count.count = 1;
        ref.value.ref = ptr;

        return ref;
    }

    return *ptr;
}

static inline struct object_t
make_empty_ref(void)
{
    return make_ref(empty_pair);
}

static inline struct object_t *
deref(struct object_t *ptr)
{
    return (ptr == NULL) ? NULL : ((ptr->tag_count.tag == TAG_REFERENCE) ? ptr->value.ref : ptr);
}

static inline const struct object_t *
const_deref(const struct object_t *ptr)
{
    return (ptr == NULL) ? NULL : ((ptr->tag_count.tag == TAG_REFERENCE) ? ptr->value.ref : ptr);
}

#endif

