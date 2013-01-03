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



/*
 * main!
 */
int 
main(void) 
{
    const char *inputs[] = {
        "4",
        "(define count (lambda (item L) (if L (+ (equal? item (first L)) (count item (rest L))) 0)))",
        "(define fact (lambda (n) (if (<= n 1) 1 (* n (fact (- n 1))))))",
        "(define hello-world (lambda () (display \"hello world!\") (newline)))",
        "(define hello-world-2 (lambda (x) `(foo ,bar ,@(list 1 2 3))))",
        "(define vector-test #(1 2 3))"
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
        struct object_t *object;
        struct object_t *result;
        struct object_t *input_object;
        struct object_t *args;
        size_t input_length = strlen(inputs[i]);
    
        args = gc_alloc(environment->heap, TAG_PAIR, 0);
        input_object = gc_alloc(environment->heap, TAG_STRING, input_length);
        memmove(input_object->value.string_value, inputs[i], input_length);
        CAR(args) = input_object;

        object = read(environment, args);
        result = eval(environment, object);
        print(environment, result);
        printf("\n");
    }

    posix_memfree(heap);

    return 0;
}
