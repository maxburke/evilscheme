/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "base.h"
#include "environment.h"
#include "evil_scheme.h"
#include "gc.h"
#include "object.h"
#include "runtime.h"
#include "vm.h"

struct evil_object_t
evil_cons(struct evil_environment_t *environment, struct evil_object_handle_t *lexical_environment, int num_args, struct evil_object_t *args)
{
    struct evil_object_t *object;

    UNUSED(environment);
    UNUSED(lexical_environment);
    UNUSED(num_args);

    assert(num_args == 2);

    object = gc_alloc(environment->heap, TAG_PAIR, 0);

    *RAW_CAR(object) = args[0];
    *RAW_CDR(object) = args[1];

    return make_ref(object);
}

struct evil_object_t
evil_define(struct evil_environment_t *environment, struct evil_object_handle_t *lexical_environment, int num_args, struct evil_object_t *args)
{
    struct evil_object_t *place;
    struct evil_object_t *value;

    UNUSED(num_args);
    UNUSED(lexical_environment);

    assert(num_args == 2);

    place = args + 0;
    value = args + 1;

    if (place->tag_count.tag == TAG_SYMBOL)
    {
        struct evil_object_t *location;

        /*
         * define binds variables in the global environment, so the lexical
         * environment parameter goes unused in this function.
         */
        location = bind(environment, environment->lexical_environment, *place);
        *location = *value;
    }
    else
    {
        /*
         * We're likley evaluating (define (x y z) ...) where the place is the
         * name of a function plus a lambda list. Maybe expressions of this
         * form can be turned into (define x (lambda (y z) ...)) by the reader?
         * Maybe down the road...
         *
         * We may also have some bad syntax here and are evaluating something
         * like (define 2 3).
         */
        BREAK();
    }

    return *value;
}

struct evil_object_t
evil_vector(struct evil_environment_t *environment, struct evil_object_handle_t *lexical_environment, int num_args, struct evil_object_t *args)
{
    struct evil_object_t *vector;
    struct evil_object_t *vector_base;
    int i;

    UNUSED(lexical_environment);

    assert(num_args < 65536);

    vector = gc_alloc_vector(environment->heap, num_args);
    vector_base = VECTOR_BASE(vector);

    for (i = 0; i < num_args; ++i)
    {
        vector_base[i] = args[i];
    }

    return make_ref(vector);
}

static int64_t
evil_coerce_fixnum(struct evil_object_t *arg)
{
    unsigned char tag;

    tag = arg->tag_count.tag;
    assert(tag == TAG_FIXNUM || tag == TAG_FLONUM);

    if (tag == TAG_FLONUM)
    {
        return (int64_t)arg->value.flonum_value;
    }

    return arg->value.fixnum_value;
}

struct evil_object_t
evil_vector_length(struct evil_environment_t *environment, struct evil_object_handle_t *lexical_environment, int num_args, struct evil_object_t *args)
{
    struct evil_object_t length;
    struct evil_object_t *vector;

    UNUSED(environment);
    UNUSED(lexical_environment);
    UNUSED(num_args);

    vector = deref(args + 0);

    assert(num_args == 1);
    assert(vector->tag_count.tag == TAG_VECTOR);

    length.tag_count.tag = TAG_FIXNUM;
    length.tag_count.flag = 0;
    length.tag_count.count = 1;
    length.value.fixnum_value = vector->tag_count.count;

    return length;
}

struct evil_object_t
evil_vector_ref(struct evil_environment_t *environment, struct evil_object_handle_t *lexical_environment, int num_args, struct evil_object_t *args)
{
    struct evil_object_t *vector;
    struct evil_object_t *element;
    int64_t index;

    UNUSED(environment);
    UNUSED(lexical_environment);
    UNUSED(num_args);

    assert(num_args == 2);

    index = 0;
    vector = deref(args + 0);
    element = deref(args + 1);

    assert(vector->tag_count.tag == TAG_VECTOR);

    index = evil_coerce_fixnum(element);
    assert(index < 65536);
    assert(index < vector->tag_count.count);

    return make_inner_reference(VECTOR_BASE(vector), index);
}

static struct evil_object_t
evil_vector_fill(struct evil_environment_t *environment, int num_args, struct evil_object_t *args)
{
    struct evil_object_t *vector;
    struct evil_object_t *vector_base;
    struct evil_object_t fill;
    int count;
    int i;

    UNUSED(environment);
    UNUSED(num_args);

    assert(num_args == 2);

    vector = deref(args + 0);
    fill = *(args + 1);

    assert(vector->tag_count.tag == TAG_VECTOR);
    count = vector->tag_count.count;
    vector_base = VECTOR_BASE(vector);

    for (i = 0; i < count; ++i)
    {
        vector_base[i] = fill;
    }

    return make_ref(vector);
}

struct evil_object_t
evil_make_vector(struct evil_environment_t *environment, struct evil_object_handle_t *lexical_environment, int num_args, struct evil_object_t *args)
{
    struct evil_object_t *vector;
    struct evil_object_t *size;
    struct evil_object_t *fill;
    size_t fixnum_size;
    struct evil_object_t vector_fill_args[2];

    UNUSED(lexical_environment);

    assert(num_args == 1 || num_args == 2);

    size = deref(args + 0);
    fill = deref(args + 1);

    assert(size->tag_count.tag == TAG_FIXNUM);
    fixnum_size = (size_t)evil_coerce_fixnum(size);

    vector = gc_alloc_vector(environment->heap, fixnum_size);
    vector_fill_args[0] = make_ref(vector);

    vector_fill_args[1] = (num_args == 2) ? *fill : make_fixnum_object(0);

    return evil_vector_fill(environment, 2, vector_fill_args);
}

struct evil_object_t
evil_apply(struct evil_environment_t *environment, struct evil_object_handle_t *lexical_environment, int num_args, struct evil_object_t *args)
{
    struct evil_object_t *fn;
    struct evil_object_t *fn_args;
    unsigned char fn_tag;
    int num_args_for_fn;

    fn = deref(args + 0);
    fn_tag = fn->tag_count.tag;
    fn_args = args + 1;
    num_args_for_fn = num_args - 1;

    if (fn_tag == TAG_SYMBOL)
    {
        struct evil_object_t *bound_location;

        bound_location = get_bound_location(environment, fn->value.symbol_hash, 1);
        assert(bound_location != NULL);

        fn = deref(bound_location);
        fn_tag = fn->tag_count.tag;
    }

    switch (fn->tag_count.tag)
    {
        case TAG_SPECIAL_FUNCTION:
            {
                evil_special_function_t special_fn;

                special_fn = fn->value.special_function_value;
                return special_fn(environment, lexical_environment, num_args_for_fn, fn_args);
            }
        case TAG_PROCEDURE:
            return vm_run(environment, lexical_environment, fn, num_args_for_fn, fn_args);
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
struct evil_object_t
evil_eval(struct evil_environment_t *environment, struct evil_object_handle_t *lexical_environment, int num_args, struct evil_object_t *args)
{
    struct evil_object_t *object;

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
                struct evil_object_t *bound_location;

                bound_location = get_bound_location_in_lexical_environment(evil_resolve_object_handle(lexical_environment), object->value.symbol_hash, 1);
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
                struct evil_object_t *wrapper_args;
                struct evil_object_t *wrapper_body;
                struct evil_object_handle_t *wrapper_args_handle;
                struct evil_object_t fn;

                wrapper_args = gc_alloc(environment->heap, TAG_PAIR, 0);
                wrapper_args_handle = evil_create_object_handle(environment, wrapper_args);

                wrapper_body = gc_alloc(environment->heap, TAG_PAIR, 0);

                wrapper_args = evil_resolve_object_handle(wrapper_args_handle);

                *RAW_CAR(wrapper_args) = make_empty_ref();
                *RAW_CDR(wrapper_args) = make_ref(wrapper_body);
                *RAW_CAR(wrapper_body) = make_ref(object);
                *RAW_CDR(wrapper_body) = make_empty_ref();

                fn = evil_lambda(environment, lexical_environment, 1, wrapper_args);
                return vm_run(environment, lexical_environment, &fn, 0, empty_pair);
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
print_impl(struct evil_environment_t *environment, int num_args, struct evil_object_t *args)
{
    struct evil_object_t *object;

    UNUSED(environment);
    UNUSED(num_args);
    assert(num_args == 1);

    object = deref(args);

    if (object == empty_pair)
    {
        evil_printf("'()");
        return;
    }

    if (object == NULL)
    {
        return;
    }

    switch (object->tag_count.tag)
    {
        case TAG_BOOLEAN:
            evil_printf("%s", object->value.fixnum_value ? "#t" : "#f");
            break;
        case TAG_CHAR:
            evil_printf("%c", (char)object->value.fixnum_value);
            break;
        case TAG_VECTOR:
            {
                int i;
                int count;
                struct evil_object_t *base;

                base = VECTOR_BASE(object);
                count = object->tag_count.count;

                evil_printf("#(");
                for (i = 0; i < count; ++i)
                {
                    print_impl(environment, 1, base + i);

                    if (i < (count - 1))
                    {
                        evil_printf(" ");
                    }
                }
                evil_printf(")");
                break;
            }
        case TAG_FIXNUM:
            evil_printf("%" PRId64, object->value.fixnum_value);
            break;
        case TAG_FLONUM:
            evil_printf("%f", object->value.flonum_value);
            break;
        case TAG_PROCEDURE:
            evil_printf("<procedure>");
            break;
        case TAG_STRING:
            evil_printf("\"%s\"", object->value.string_value);
            break;
        case TAG_SYMBOL:
            {
                const char *str = find_symbol_name(environment, object->value.symbol_hash);
                evil_printf("'%s", str);
                break;
            }
        case TAG_PAIR:
            if (CAR(object)->tag_count.tag == TAG_PAIR)
            {
                evil_printf("(");
            }

            print_impl(environment, 1, CAR(object));

            if (CDR(object) != empty_pair)
            {
                evil_printf(" ");
                print_impl(environment, 1, CDR(object));
            }
            else
            {
                evil_printf(")");
            }
            break;
        case TAG_SPECIAL_FUNCTION:
            evil_printf("<special function>");
            break;
        default:
            assert(0);
            break;
    }
}

struct evil_object_t
evil_print(struct evil_environment_t *environment, struct evil_object_handle_t *lexical_environment, int num_args, struct evil_object_t *args)
{
    UNUSED(lexical_environment);

    print_impl(environment, num_args, args);
    evil_printf("\n");

    return make_empty_ref();
}

