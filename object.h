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
    TAG_INVALID,            /* 0 */
    TAG_BOOLEAN,            /* 1 */
    TAG_SYMBOL,             /* 2 */
    TAG_CHAR,               /* 3 */
    TAG_VECTOR,             /* 4 */
    TAG_PAIR,               /* 5 */
    TAG_FIXNUM,             /* 6 */
    TAG_FLONUM,             /* 7 */
    TAG_STRING,             /* 8 */
    TAG_PROCEDURE,          /* 9 */

    /*
     * Special functions include read, eval, print, define. These functions 
     * are part of the core but must also be accessible and usable from within
     * the script.
     */
    TAG_SPECIAL_FUNCTION,   /* a */

    /*
     * The environment and heap are both given object tags as they will both
     * be traversed during garbage collection.
     */
    TAG_ENVIRONMENT,        /* b */
    TAG_HEAP,               /* c */

    /*
     * References are how aggregate objects are handled within the system
     * in most cases. For example, a vector will exist on the heap but 
     * it is a reference to it that will be stored in a pair, or on the stack,
     * or within another vector.
     */
    TAG_REFERENCE,          /* d */

    /*
     * Inner references are a type of reference to the inside of an aggregate
     * object like a vector or string and are comparable to .NET's managed
     * pointers. Typically these are created with functions like vector-ref,
     * string-ref, vector-set!, string-set!
     */
    TAG_INNER_REFERENCE     /* e */
};

struct tag_count_t
{
    unsigned char tag;
    unsigned char flag;
    unsigned short count;
};

struct object_t;
struct environment_t;
typedef struct object_t (*special_function_t)(struct environment_t *, int, struct object_t *);

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

/*
 * These macros are used to make it easy to index into vectors and access the
 * innards of pairs. Pairs are implemented as vectors-of-length-two for the
 * sake of simplicity, so (car x) is equivalent to (vector-ref x 0) basically.
 * These types are tagged different though, for now, for aiding debugging in
 * the parser.
 */
#define VECTOR_BASE(x) ((struct object_t *)(&(x)->value))
#define RAW_CAR(x) (VECTOR_BASE(x))
#define RAW_CDR(x) (VECTOR_BASE(x) + 1)
#define CAR(x) deref((VECTOR_BASE(x)))
#define CDR(x) deref((VECTOR_BASE(x) + 1))

/*
 * make_ref creates a reference to the specified object. The VM assumes that
 * only value types (boolean/char/fixnum/flonum/references) exist on the
 * evaluation stack, and that compound types (pairs, vectors) only hold value
 * types. Basically this means that all reference types like pairs, vectors, or
 * strings are handled through references.
 *
 * This is confusing but it makes several parts of the compiler easier to
 * understand and implement, specifically the code that handles pairs as well
 * as the VM stack.
 */
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

/*
 * This helper method creates a reference to the empty pair object. The
 * compiler shouldn't ever barf on a NULL value as the empty pair is used
 * instead to indicate the end of a list or indeterminate values (ie:
 * functions with no defined return type.)
 */
static inline struct object_t
make_empty_ref(void)
{
    return make_ref(empty_pair);
}

static inline struct object_t
make_fixnum_object(int64_t value)
{
    struct object_t object;

    object.tag_count.tag = TAG_FIXNUM;
    object.tag_count.flag = 0;
    object.tag_count.count = 1;
    object.value.fixnum_value = value;

    return object;
}

/*
 * Dereference an object if necessary.
 * TODO: What should this do for inner reference types?
 */
static inline struct object_t *
deref(struct object_t *ptr)
{
    return (ptr == NULL) ? NULL : ((ptr->tag_count.tag == TAG_REFERENCE) ? ptr->value.ref : ptr);
}

/*
 * Like deref but takes a pointer-to-const-object and returns a 
 * pointer-to-const-object.
 */
static inline const struct object_t *
const_deref(const struct object_t *ptr)
{
    return (ptr == NULL) ? NULL : ((ptr->tag_count.tag == TAG_REFERENCE) ? ptr->value.ref : ptr);
}

#endif

