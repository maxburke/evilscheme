#ifndef OBJECT_H
#define OBJECT_H

#include <stddef.h>
#include <stdint.h>

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
typedef struct object_t *(*special_function_t)(struct environment_t *, struct object_t *);

struct inner_reference_t
{
    struct object_t *object;
    int64_t index;
};

union object_value_t
{
    int64_t fixnum_value;
    double flonum_value;
    uint64_t symbol_hash;
    special_function_t special_function_value;
    char string_value[1];
    struct object_t *pair[2];
    struct inner_reference_t ref;
};

struct object_t
{
    struct tag_count_t tag_count;
    union object_value_t value;
};

extern struct object_t *empty_pair;

uint64_t
hash_string(const char *string);

uint64_t
hash_bytes(const void *bytes, size_t num_bytes);

#define CAR(x) ((x)->value.pair[0])
#define CDR(x) ((x)->value.pair[1])

#endif

