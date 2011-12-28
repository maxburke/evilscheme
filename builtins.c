#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "base.h"
#include "builtins.h"
#include "object.h"
#include "environment.h"

struct object_t *
quote(struct environment_t *environment, struct object_t *args)
{
    UNUSED(environment);
    assert(args->tag_count.tag == TAG_PAIR);

    return CAR(args);
}

struct object_t *
cons(struct environment_t *environment, struct object_t *args)
{
    struct object_t *object;

    UNUSED(environment);
    
    assert(args->tag_count.tag == TAG_PAIR);
    assert(CDR(args)->tag_count.tag == TAG_PAIR);

    object = allocate_object(TAG_PAIR, 0);

    CAR(object) = CAR(args);
    CDR(object) = CAR(CDR(args));

    return object;
}

struct object_t *
car(struct environment_t *environment, struct object_t *args)
{
    UNUSED(environment);
    assert(args->tag_count.tag == TAG_PAIR);
    return CAR(args);
}

struct object_t *
cdr(struct environment_t *environment, struct object_t *args)
{
    UNUSED(environment);
    assert(args->tag_count.tag == TAG_PAIR);
    return CDR(args);
}


struct object_t *
set(struct environment_t *environment, struct object_t *args)
{
    UNUSED(environment);
    assert(args->tag_count.tag == TAG_PAIR);
    assert(CDR(args)->tag_count.tag == TAG_PAIR);
    BREAK();
    return NULL;
}

struct object_t *
define(struct environment_t *environment, struct object_t *args)
{
    /* fetch symbol binding location */
    /* evaluate argument */
    /* call set! on it */
    /* TODO: I think this incorrect, I don't think we need to evaluate place, it will
       either be a variable or a list of (variable lambda-list*) */
    struct object_t *place;
    struct object_t *value;

    assert(args->tag_count.tag == TAG_PAIR);
    assert(CDR(args)->tag_count.tag == TAG_PAIR);
    place = CAR(args);
    value = eval(environment, CDR(args));
    
    assert(place->tag_count.tag == TAG_SYMBOL || place->tag_count.tag == TAG_PAIR);
    if (place->tag_count.tag == TAG_SYMBOL)
    {
        struct object_t **location = bind(environment, args);
        *location = value;
    }
    else
    {
    }

    return value;
}

struct object_t *
lambda(struct environment_t *environment, struct object_t *args)
{
    UNUSED(environment);
    UNUSED(args);
    BREAK();
    return NULL;
}

struct object_t *
apply(struct environment_t *environment, struct object_t *args)
{
    struct object_t **bound_location;
    struct object_t *function_obj;
    struct object_t *function_args;

    bound_location = get_bound_location(environment, CAR(args), 1);
    assert(bound_location != NULL);
    function_args = CDR(args);
    function_obj = *bound_location;

    switch (function_obj->tag_count.tag)
    {
        case TAG_SPECIAL_FUNCTION:
            return function_obj->value.special_function_value(environment, function_args);
        case TAG_PROCEDURE:
            BREAK();
            break;
        default:
            BREAK();
            break;
    }

    return NULL;
}

/*
 * eval
 */

struct object_t *
eval(struct environment_t *environment, struct object_t *args)
{
    struct object_t *first_arg;
    struct object_t **bound_location;
    assert(args->tag_count.tag == TAG_PAIR);

    first_arg = CAR(args);
    switch (first_arg->tag_count.tag)
    {
        case TAG_SPECIAL_FUNCTION:
            return first_arg->value.special_function_value(environment, CDR(args));
        case TAG_BOOLEAN:
        case TAG_CHAR:
        case TAG_VECTOR:
        case TAG_FIXNUM:
        case TAG_FLONUM:
        case TAG_PROCEDURE:
        case TAG_STRING:
            return first_arg;
        case TAG_SYMBOL:
            bound_location = get_bound_location(environment, first_arg, 1);
            assert(bound_location != NULL);
            return *bound_location;
        case TAG_PAIR:
            return apply(environment, first_arg);
            break;
    }

    BREAK();
    return NULL;
}

/*
 * print
 */

struct object_t *
print(struct environment_t *environment, struct object_t *args)
{
    UNUSED(environment);

    if (!args)
        return NULL;

    if (args == empty_pair)
    {
        printf("'()");
        return NULL;
    }

    switch (args->tag_count.tag)
    {
        case TAG_BOOLEAN:
            printf("%s", args->value.boolean_value ? "#t" : "#f");
            return args + 1;
        case TAG_CHAR:
            printf("%c", args->value.char_value);
            return args + 1;
        case TAG_VECTOR:
            {
                int i;
                struct object_t *object = args + 1;

                printf("#(");
                for (i = 0; i < args->tag_count.count; ++i)
                    object = print(environment, object);
                printf(")");
                return object;
            }
        case TAG_FIXNUM:
            printf("%ld", args->value.fixnum_value);
            return args + 1;
        case TAG_FLONUM:
            printf("%f", args->value.flonum_value);
            return args + 1;
        case TAG_PROCEDURE:
            printf("<procedure>");
            return args + 1;
        case TAG_STRING:
            printf("\"%s\"", args->value.string_value);
            return (struct object_t *)((char *)(args + 1) + strlen(args->value.string_value));
        case TAG_SYMBOL:
            printf("%s", args->value.string_value);
            return (struct object_t *)((char *)(args + 1) + strlen(args->value.string_value));
        case TAG_PAIR:
            print(environment, CAR(args));
            printf(" ");
            print(environment, CDR(args));
            return args + 1;
        case TAG_SPECIAL_FUNCTION:
            printf("<special function>");
            return args + 1;
        default:
            assert(0);
            break;
    }

    return NULL;
}



