/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#ifndef EVIL_USER_H
#define EVIL_USER_H

#include <stdint.h>
#include <stddef.h>

/*
 * The user is responsible for implementing these functions.
 */

void *
evil_aligned_alloc(size_t alignment, size_t size);

void
evil_aligned_free(void *ptr);

void
evil_printf(const char *format, ...);

void
evil_debug_printf(const char *format, ...);

/*
 * This section contains the types and tags used by the object system.
 */

enum evil_tag_t
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
    TAG_EXTERNAL_FUNCTION,  /* b */

    /*
     * The environment and heap are both given object tags as they will both
     * be traversed during garbage collection.
     */
    TAG_ENVIRONMENT,        /* c */

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

struct evil_tag_count_t
{
    unsigned char tag;
    unsigned char flag;
    unsigned short count;
};

struct evil_object_t;
struct evil_environment_t;
typedef struct evil_object_t (*evil_special_function_t)(struct evil_environment_t *, int, struct evil_object_t *);

union evil_object_value_t
{
    int64_t fixnum_value;
    double flonum_value;
    uint64_t symbol_hash;
    evil_special_function_t special_function_value;
    char string_value[1];
    struct evil_object_t *ref;
};

struct evil_object_t
{
    struct evil_tag_count_t tag_count;
    union evil_object_value_t value;
};

/*
 * The garbage collector needs to know about all references to heap objects
 * and, if they are kept by code outside of the Scheme, must be tracked via 
 * these object handles.
 */
struct evil_object_handle_t;

struct evil_object_handle_t *
evil_create_object_handle(struct evil_environment_t *environment, struct evil_object_t *object);

struct evil_object_handle_t *
evil_create_object_handle_from_value(struct evil_environment_t *environment, struct evil_object_t object);

void
evil_destroy_object_handle(struct evil_environment_t *environment, struct evil_object_handle_t *handle);

struct evil_object_t *
evil_resolve_object_handle(struct evil_object_handle_t *handle);

/*
 * These functions provide the initial core functions used by evil scheme's 
 * runtime.
 */

struct evil_object_t
evil_read(struct evil_environment_t *environment, int num_args, struct evil_object_t *object);

struct evil_object_t
evil_eval(struct evil_environment_t *environment, int num_args, struct evil_object_t *object);

struct evil_object_t
evil_print(struct evil_environment_t *environment, int num_args, struct evil_object_t *object);

struct evil_object_t
evil_cons(struct evil_environment_t *environment, int num_args, struct evil_object_t *object);

struct evil_object_t
evil_define(struct evil_environment_t *environment, int num_args, struct evil_object_t *object);

struct evil_object_t
evil_lambda(struct evil_environment_t *environment, int num_args, struct evil_object_t *object);

struct evil_object_t
evil_disassemble(struct evil_environment_t *environment, int num_args, struct evil_object_t *object);

struct evil_object_t
evil_apply(struct evil_environment_t *environment, int num_args, struct evil_object_t *object);

struct evil_object_t
evil_vector(struct evil_environment_t *environment, int num_args, struct evil_object_t *object);

#endif
