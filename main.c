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

#ifdef _MSC_VER
    #include <malloc.h>

    static int
    posix_memalign(void **ptr, size_t alignment, size_t size)
    {
        void *mem;

        mem = _aligned_malloc(size, alignment);
        *ptr = mem;

        return mem != 0;
    }

    static void
    posix_memfree(void *ptr)
    {
        _aligned_free(ptr);
    }
#else
    static void
    posix_memfree(void *ptr)
    {
        free(ptr);
    }
#endif

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
        "4",
        "(define list-length (lambda (L) (if (null? L) 0 (+ 1 (list-length (rest L))))))",
        "(disassemble 'list-length)",
        "(define list-length-tailrec (lambda (L x) (if (null? L) x (list-length-tailrec (rest L) (+ 1 x)))))",
        "(disassemble 'list-length-tailrec)",
        "(define fact (lambda (n) (if (<= n 1) 1 (* n (fact (- n 1))))))",
        "(disassemble 'fact)",
        "(fact 5)",
        "(define fact-tailrec (lambda (acc i limit) (if (> i limit) acc (fact-tailrec (* i acc) (+ i 1) limit))))",
        "(disassemble 'fact-tailrec)",
        "(fact-tailrec 1 1 5)",
        "(define count (lambda (item L) (if L (+ (equal? item (first L)) (count item (rest L))) 0)))",
        "(disassemble 'count)",
        "(define hello-world (lambda () (display \"hello world!\") (newline)))",
        "(disassemble 'hello-world)",
        "(define hello-world-2 (lambda (x) `(foo ,bar ,@(list 1 2 3))))",
        "(disassemble 'hello-world-2)",
        "(define vector-test #(1 2 3))",
        "vector-test",
        "(define let-test (lambda (foo) (let ((x 1) (y (+ 1 x))) (+ x foo y))))",
        "(disassemble 'let-test)",
        "(let-test 3)",
        "(define closure-test (lambda (foo) (lambda () (set! foo (1+ foo)) foo)))",
        "(define closure-test-fn (closure-test))",
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
    posix_memalign(&heap, 4096, heap_size);

    environment = environment_create(stack, stack_size, heap, heap_size);

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

    posix_memfree(heap);

    return 0;
}

