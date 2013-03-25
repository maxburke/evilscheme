/***********************************************************************
 * evilscheme, Copyright (c) 2012-2013, Maximilian Burke
 * This file is distributed under the FreeBSD license.
 * See LICENSE.TXT for details.
 ***********************************************************************/

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "object.h"
#include "builtins.h"
#include "environment.h"
#include "gc.h"
#include "read.h"
#include "runtime.h"
#include "user.h"

#ifdef _MSC_VER
    #include <malloc.h>

    void *
    evil_aligned_alloc(size_t alignment, size_t size)
    {
        return _aligned_malloc(size, alignment);
    }

    void
    evil_aligned_free(void *ptr)
    {
        _aligned_free(ptr);
    }
#else
    void *
    evil_aligned_alloc(size_t alignment, size_t size)
    {
        void *mem;

        posix_memalign(&mem, alignment, size);

        return mem;
    }

    void
    evil_aligned_free(void *ptr)
    {
        free(ptr);
    }
#endif

#ifdef EVIL_RUN_TESTS

#include "test.h"

int
main(void)
{
    return evil_run_tests();
}

#else

void
evil_print(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

/*
 * main!
 */
int
main(void)
{
    const char *inputs[] = {
        "(define closure-test (lambda (foo) (lambda () (set! foo (+ 1 foo)) foo)))",
        "(define closure-test-fn (closure-test 0))",
        "(closure-test-fn)",
        "(closure-test-fn)",
    };
    size_t i;

    struct environment_t *environment;
    void *stack;
    void *heap;
    size_t stack_size = 1024 * sizeof(void *);
    size_t heap_size = 1024 * 1024;

    stack = malloc(stack_size);
    heap = evil_aligned_alloc(4096, heap_size);

    environment = evil_environment_create(stack, stack_size, heap, heap_size);

    for (i = 0; i < sizeof inputs / sizeof inputs[0]; ++i)
    {
        struct object_t *input_object;
        struct object_t *args;
        struct object_t arg_ref;
        struct object_t object;
        struct object_t result;
        size_t input_length = strlen(inputs[i]);

        args = gc_alloc(environment->heap, TAG_PAIR, 0);
        input_object = gc_alloc(environment->heap, TAG_STRING, input_length);
        memmove(input_object->value.string_value, inputs[i], input_length);

        *CAR(args) = make_ref(input_object);
        arg_ref = make_ref(args);
        object = read(environment, &arg_ref);
        result = eval(environment, &object);
        print(environment, &result);

        evil_print("\n");
    }

    evil_aligned_free(heap);

    return 0;
}

#endif

