#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "base.h"
#include "builtins.h"
#include "environment.h"
#include "gc.h"
#include "object.h"
#include "runtime.h"
#include "vm.h"

struct object_t
quote(struct environment_t *environment, struct object_t *args)
{
    UNUSED(environment);
    assert(args->tag_count.tag == TAG_PAIR);
    return *CAR(args);
}

struct object_t
cons(struct environment_t *environment, struct object_t *args)
{
    struct object_t *object;

    UNUSED(environment);
    
    assert(args->tag_count.tag == TAG_PAIR);
    assert(CDR(args)->tag_count.tag == TAG_PAIR);

    object = gc_alloc(environment->heap, TAG_PAIR, 0);

    *CAR(object) = *CAR(args);
    *CDR(object) = *CAR(CDR(args));

    return make_ref(object);
}

struct object_t
car(struct environment_t *environment, struct object_t *args)
{
    struct object_t *object;

    UNUSED(environment);

    object = deref(args);
    assert(object->tag_count.tag == TAG_PAIR);

    return *CAR(object);
}

struct object_t
cdr(struct environment_t *environment, struct object_t *args)
{
    struct object_t *object;

    UNUSED(environment);

    object = deref(args);
    assert(object->tag_count.tag == TAG_PAIR);

    return *CDR(object);
}


struct object_t
set(struct environment_t *environment, struct object_t *args)
{
    struct object_t *object;

    UNUSED(environment);

    object = deref(args);
    assert(object->tag_count.tag == TAG_PAIR);
    assert(CDR(object)->tag_count.tag == TAG_PAIR);
    BREAK();

    return make_empty_ref();
}

struct object_t
define(struct environment_t *environment, struct object_t *args)
{
    /* fetch symbol binding location */
    /* evaluate argument */
    /* call set! on it */
    /* TODO: I think this incorrect, I don't think we need to evaluate place, it will
       either be a variable or a list of (variable lambda-list*) */
    struct object_t *object;
    struct object_t *place;
    struct object_t *rest;
    struct object_t value;

    object = deref(args);
    assert(object->tag_count.tag == TAG_PAIR);

    rest = CDR(object);
    assert(rest->tag_count.tag == TAG_PAIR);
    place = CAR(object);
    value = eval(environment, rest);
    
    assert(place->tag_count.tag == TAG_SYMBOL || place->tag_count.tag == TAG_PAIR);
    if (place->tag_count.tag == TAG_SYMBOL)
    {
        struct object_t *location = bind(environment, object);
        *location = value;
    }
    else
    {
        BREAK();
        /* Here is where the "place" evaluates to a function + lambda list */
    }

    return value;
}

struct object_t
vector(struct environment_t *environment, struct object_t *args)
{
    struct object_t *object;
    struct object_t *i;
    struct object_t *vector;
    struct object_t *base;
    size_t idx;

    object = deref(args);
    idx = 0;

    for (i = object; i != empty_pair; i = CDR(i), ++idx)
        ;

    assert(idx < 65536);

    vector = gc_alloc(environment->heap, TAG_VECTOR, sizeof(struct object_t) * idx);
    vector->tag_count.count = (unsigned short)idx;

    base = VECTOR_BASE(vector);
    idx = 0;

    for (i = object; i != empty_pair; i = CDR(i), ++idx)
    {
        struct object_t value;

        value = eval(environment, i);

        switch (value.tag_count.tag)
        {
            case TAG_BOOLEAN:
            case TAG_SYMBOL:
            case TAG_CHAR:
            case TAG_FIXNUM:
            case TAG_FLONUM:
            case TAG_PAIR:
            case TAG_REFERENCE:
            case TAG_INNER_REFERENCE:
                base[idx] = value;
                break;
            case TAG_VECTOR:
            case TAG_STRING:
            case TAG_PROCEDURE:
            case TAG_SPECIAL_FUNCTION:
                /*
                 * Only references to these types should ever be returned here.
                 * This is an error condition.
                 */
                BREAK();
                break;
            case TAG_ENVIRONMENT:
            case TAG_HEAP:
                BREAK();
                break;
        }
    }

    return make_ref(vector);
}

struct object_t
apply(struct environment_t *environment, struct object_t *args)
{
    struct object_t *object;
    struct object_t *bound_location;
    struct object_t *function_args;
    struct object_t *function;

    object = deref(args);
    function = CAR(object);
    function_args = CDR(object);

    if (function->tag_count.tag == TAG_SYMBOL)
    {
        bound_location = get_bound_location(environment, function, 1);
        assert(bound_location != NULL);
        function = bound_location;
    }

    switch (function->tag_count.tag)
    {
        case TAG_SPECIAL_FUNCTION:
            return function->value.special_function_value(environment, function_args);
        case TAG_PROCEDURE:
            return vm_run(environment, function, object);
        default:
            BREAK();
            break;
    }

    return make_empty_ref();
}

/*
 * eval
 */

struct object_t
eval(struct environment_t *environment, struct object_t *args)
{
    struct object_t *object;
    struct object_t *first_arg;
    struct object_t *bound_location;

    object = deref(args);

    assert(object->tag_count.tag == TAG_PAIR);

    first_arg = CAR(object);
    switch (first_arg->tag_count.tag)
    {
        case TAG_SPECIAL_FUNCTION:
            return first_arg->value.special_function_value(environment, CDR(object));
        case TAG_BOOLEAN:
        case TAG_CHAR:
        case TAG_FIXNUM:
        case TAG_FLONUM:
            return *first_arg;
        case TAG_VECTOR:
        case TAG_PROCEDURE:
        case TAG_STRING:
            return make_ref(first_arg);
        case TAG_SYMBOL:
            bound_location = get_bound_location(environment, first_arg, 1);
            assert(bound_location != NULL);
            return *bound_location;
        case TAG_PAIR:
            return apply(environment, first_arg);
            break;
    }

    BREAK();
    return make_empty_ref();
}

/*
 * print
 */

struct object_t
print(struct environment_t *environment, struct object_t *args)
{
    struct object_t *object;

    UNUSED(environment);
    object = deref(args);

    if (object == empty_pair)
    {
        skim_print("'()");
        return make_empty_ref();
    }

    switch (object->tag_count.tag)
    {
        case TAG_BOOLEAN:
            skim_print("%s", object->value.fixnum_value ? "#t" : "#f");
            break;
        case TAG_CHAR:
            skim_print("%c", (char)object->value.fixnum_value);
            break; 
        case TAG_VECTOR:
            {
                int i;
                int count;
                struct object_t *base;
                
                base = VECTOR_BASE(object);
                count = object->tag_count.count;

                skim_print("#(");
                for (i = 0; i < count; ++i)
                {
                    print(environment, base + i);

                    if (i < (count - 1))
                    {
                        skim_print(" ");
                    }
                }
                skim_print(")");
                break;
            }
        case TAG_FIXNUM:
            skim_print("%" PRId64, object->value.fixnum_value);
            break;
        case TAG_FLONUM:
            skim_print("%f", object->value.flonum_value);
            break;
        case TAG_PROCEDURE:
            skim_print("<procedure>");
            break;
        case TAG_STRING:
            skim_print("\"%s\"", object->value.string_value);
            break;
        case TAG_SYMBOL:
            {
                const char *str = find_symbol_name(environment, object->value.symbol_hash);
                skim_print("%s", str);
                break;
            }
        case TAG_PAIR:
            print(environment, CAR(object));
            skim_print(" ");
            print(environment, CDR(object));
            break;
        case TAG_SPECIAL_FUNCTION:
            skim_print("<special function>");
            break;
        default:
            assert(0);
            break;
    }

    return make_empty_ref();
}



