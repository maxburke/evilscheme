/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license. 
 * See LICENSE.TXT for details.
 ***********************************************************************/

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
quote(struct environment_t *environment, int num_args, struct object_t *args)
{
    UNUSED(environment);
    UNUSED(num_args);

    assert(num_args == 1);

    return *args;
}

struct object_t
cons(struct environment_t *environment, int num_args, struct object_t *args)
{
    struct object_t *object;

    UNUSED(environment);
    UNUSED(num_args);

    assert(num_args == 2);
    
    object = gc_alloc(environment->heap, TAG_PAIR, 0);

    *RAW_CAR(object) = args[0];
    *RAW_CDR(object) = args[1];

    return make_ref(object);
}

struct object_t
car(struct environment_t *environment, int num_args, struct object_t *args)
{
    struct object_t *object;

    UNUSED(environment);
    UNUSED(num_args);

    assert(num_args == 1);

    object = deref(args);
    assert(object->tag_count.tag == TAG_PAIR);

    return *CAR(object);
}

struct object_t
cdr(struct environment_t *environment, int num_args, struct object_t *args)
{
    struct object_t *object;

    UNUSED(environment);
    UNUSED(num_args);

    assert(num_args == 1);

    object = deref(args);
    assert(object->tag_count.tag == TAG_PAIR);

    return *CDR(object);
}


struct object_t
set(struct environment_t *environment, int num_args, struct object_t *args)
{
    struct object_t *object;
    struct object_t *reference;
    struct object_t *target;
    unsigned char ref_type;

    UNUSED(environment);
    UNUSED(num_args);

    assert(num_args == 2);

    reference = args + 0;
    ref_type = reference->tag_count.tag;
    target = reference->value.ref;

    object = args + 1;
    
    if (ref_type == TAG_INNER_REFERENCE)
    {
        unsigned char target_type;
        unsigned short offset;

        target_type = target->tag_count.tag;
        offset = reference->tag_count.count;

        if (target_type == TAG_STRING)
        {
            char *ptr;

            assert(object->tag_count.tag == TAG_CHAR);
            ptr = target->value.string_value + offset;
            *ptr = (char)object->value.fixnum_value;
        }
        else
        {
            struct object_t *resolved_reference;
            assert(target_type == TAG_VECTOR);
            resolved_reference = &VECTOR_BASE(object)[offset];
            *resolved_reference = *object;
        }
    }
    else
    {
        assert(ref_type == TAG_REFERENCE);

        *target = *object;
    }

    return make_empty_ref();
}

struct object_t
define(struct environment_t *environment, int num_args, struct object_t *args)
{
    struct object_t *place;
    struct object_t *value;
    UNUSED(num_args);

    assert(num_args == 2);

    place = args + 0;
    value = args + 1;

    if (place->tag_count.tag == TAG_SYMBOL)
    {
        struct object_t *location;

        location = bind(environment, *place);
        *location = *value;
    }
    else
    {
        /*
         * We're evaluating (define (x y z) ...) where the place is the name of
         * a function plus a lambda list. Maybe expressions of this form can be
         * turned into (define x (lambda (y z) ...)) by the reader? Maybe down
         * the road...
         */
        BREAK();
    }

    return *value;
}

struct object_t
vector(struct environment_t *environment, int num_args, struct object_t *args)
{
    struct object_t *vector;
    struct object_t *vector_base;
    int i;

    assert(num_args < 65536);

    vector = gc_alloc_vector(environment->heap, num_args);
    vector_base = VECTOR_BASE(vector);

    for (i = 0; i < num_args; ++i)
    {
        vector_base[i] = args[i];
    }

    return make_ref(vector);
}

struct object_t
apply(struct environment_t *environment, int num_args, struct object_t *args)
{
    struct object_t *fn;
    struct object_t *fn_args;
    unsigned char fn_tag;
    int num_args_for_fn;

    fn = deref(args + 0);
    fn_tag = fn->tag_count.tag;
    fn_args = args + 1;
    num_args_for_fn = num_args - 1;

    if (fn_tag == TAG_SYMBOL)
    {
        struct object_t *bound_location;

        bound_location = get_bound_location(environment, fn->value.symbol_hash, 1);
        assert(bound_location != NULL);

        fn = deref(bound_location);
        fn_tag = fn->tag_count.tag;
    }

    switch (fn->tag_count.tag)
    {
        case TAG_SPECIAL_FUNCTION:
            {
                special_function_t special_fn;

                special_fn = fn->value.special_function_value;
                return special_fn(environment, num_args_for_fn, fn_args);
            }
        case TAG_PROCEDURE:
            return vm_run(environment, fn, num_args_for_fn, fn_args);
            break;
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
eval(struct environment_t *environment, int num_args, struct object_t *args)
{
    struct object_t *object;

    assert(num_args == 1);

    object = deref(CAR(args));
    switch (object->tag_count.tag)
    {
        case TAG_BOOLEAN:
        case TAG_CHAR:
        case TAG_FIXNUM:
        case TAG_FLONUM:
            return *object;
        case TAG_VECTOR:
        case TAG_SPECIAL_FUNCTION:
        case TAG_PROCEDURE:
        case TAG_STRING:
            return *args;
        case TAG_SYMBOL:
            {
                struct object_t *bound_location;

                bound_location = get_bound_location(environment, object->value.symbol_hash, 1);
                assert(bound_location != NULL);
                return *bound_location;
            }
        case TAG_PAIR:
            {
                /*
                 * Let's try something new and exciting here. Instead of
                 * calling apply(...), which is dead easy, let's try turning
                 * our expression into ((lambda () expr)) -- ie: (+ 1 1)
                 * becomes ((lambda () (+ 1 1)). If we don't do this then
                 * we need to have separate implementations, both VM and
                 * interpreted, for every bit of functionality supported
                 * by the runtime. And that's just not lazy!
                 */
                struct object_t *wrapper_args;
                struct object_t *wrapper_body;
                struct object_t fn;

                /*
                 * TODO: This needs to use object handles instead.
                 */
                wrapper_args = gc_alloc(environment->heap, TAG_PAIR, 0);
                wrapper_body = gc_alloc(environment->heap, TAG_PAIR, 0);

                *RAW_CAR(wrapper_args) = make_empty_ref();
                *RAW_CDR(wrapper_args) = make_ref(wrapper_body);
                *RAW_CAR(wrapper_body) = make_ref(object);
                *RAW_CDR(wrapper_body) = make_empty_ref();

                fn = lambda(environment, 1, wrapper_args);
                return vm_run(environment, &fn, 0, empty_pair);
            }
            break;
        default:
            break;
    }

    BREAK();
    return make_empty_ref();
}

/*
 * print
 */
static void
print_impl(struct environment_t *environment, int num_args, struct object_t *args)
{
    struct object_t *object;

    UNUSED(environment);
    UNUSED(num_args);
    assert(num_args == 1);

    object = deref(args);

    if (object == empty_pair)
    {
        evil_print("'()");
        return;
    }

    switch (object->tag_count.tag)
    {
        case TAG_BOOLEAN:
            evil_print("%s", object->value.fixnum_value ? "#t" : "#f");
            break;
        case TAG_CHAR:
            evil_print("%c", (char)object->value.fixnum_value);
            break;
        case TAG_VECTOR:
            {
                int i;
                int count;
                struct object_t *base;

                base = VECTOR_BASE(object);
                count = object->tag_count.count;

                evil_print("#(");
                for (i = 0; i < count; ++i)
                {
                    print_impl(environment, 1, base + i);

                    if (i < (count - 1))
                    {
                        evil_print(" ");
                    }
                }
                evil_print(")");
                break;
            }
        case TAG_FIXNUM:
            evil_print("%" PRId64, object->value.fixnum_value);
            break;
        case TAG_FLONUM:
            evil_print("%f", object->value.flonum_value);
            break;
        case TAG_PROCEDURE:
            evil_print("<procedure>");
            break;
        case TAG_STRING:
            evil_print("\"%s\"", object->value.string_value);
            break;
        case TAG_SYMBOL:
            {
                const char *str = find_symbol_name(environment, object->value.symbol_hash);
                evil_print("%s", str);
                break;
            }
        case TAG_PAIR:
            if (CAR(object)->tag_count.tag == TAG_PAIR)
            {
                evil_print("(");
            }

            print_impl(environment, 1, CAR(object));

            if (CDR(object) != empty_pair)
            {
                evil_print(" ");
                print_impl(environment, 1, CDR(object));
            }
            else
            {
                evil_print(")");
            }
            break;
        case TAG_SPECIAL_FUNCTION:
            evil_print("<special function>");
            break;
        default:
            assert(0);
            break;
    }
}

struct object_t
print(struct environment_t *environment, int num_args, struct object_t *args)
{
    print_impl(environment, num_args, args);
    evil_print("\n");

    return make_empty_ref();
}

