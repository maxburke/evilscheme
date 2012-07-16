#ifndef OBJECT_H
#define OBJECT_H

#include <stddef.h>

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
    TAG_SPECIAL_FUNCTION,
    TAG_ENVIRONMENT,
    TAG_HEAP
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

union object_value_t
{
    int boolean_value;
    long fixnum_value;
    double flonum_value;
    char char_value;
    special_function_t special_function_value;
    char string_value[1];
    struct object_t *pair[2];
};

struct object_t
{
    struct tag_count_t tag_count;
    union object_value_t value;
};

extern struct object_t *empty_pair;

#define CAR(x) ((x)->value.pair[0])
#define CDR(x) ((x)->value.pair[1])

#endif

