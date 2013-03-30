/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#ifndef EVIL_OBJECT_H
#define EVIL_OBJECT_H

#include <stddef.h>
#include <stdint.h>
#include "base.h"

extern struct evil_object_t *empty_pair;

const char *
type_name(enum evil_tag_t tag);

/*
 * These macros are used to make it easy to index into vectors and access the
 * innards of pairs. Pairs are implemented as vectors-of-length-two for the
 * sake of simplicity, so (car x) is equivalent to (vector-ref x 0) basically.
 * These types are tagged different though, for now, for aiding debugging in
 * the parser.
 */
#define VECTOR_BASE(x) ((struct evil_object_t *)(&(x)->value))
#define RAW_CAR(x) (VECTOR_BASE(x))
#define RAW_CDR(x) (VECTOR_BASE(x) + 1)
#define CAR(x) deref((VECTOR_BASE(x)))
#define CDR(x) deref((VECTOR_BASE(x) + 1))

static inline int
is_reference_tag(unsigned char tag)
{
    return (tag == TAG_PAIR || tag == TAG_VECTOR || tag == TAG_PROCEDURE || tag == TAG_STRING || tag == TAG_SPECIAL_FUNCTION || tag == TAG_ENVIRONMENT);
}

static inline int
is_value_tag(unsigned char tag)
{
    return !is_reference_tag(tag);
}

static inline int
is_reference_type(struct evil_object_t *ptr)
{
    return is_reference_tag(ptr->tag_count.tag);
}

static inline int
is_value_type(struct evil_object_t *ptr)
{
    return !is_reference_type(ptr);
}

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
static inline struct evil_object_t
make_ref(struct evil_object_t *ptr)
{
    struct evil_object_t ref;

    if (is_reference_type(ptr))
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
static inline struct evil_object_t
make_empty_ref(void)
{
    return make_ref(empty_pair);
}

static inline struct evil_object_t
make_fixnum_object(int64_t value)
{
    struct evil_object_t object;

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
static inline struct evil_object_t *
deref(struct evil_object_t *ptr)
{
    return (ptr == NULL) ? NULL : ((ptr->tag_count.tag == TAG_REFERENCE) ? ptr->value.ref : ptr);
}

/*
 * Like deref but takes a pointer-to-const-object and returns a
 * pointer-to-const-object.
 */
static inline const struct evil_object_t *
const_deref(const struct evil_object_t *ptr)
{
    return (ptr == NULL) ? NULL : ((ptr->tag_count.tag == TAG_REFERENCE) ? ptr->value.ref : ptr);
}

#endif

