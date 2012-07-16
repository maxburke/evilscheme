#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "builtins.h"
#include "environment.h"
#include "gc.h"
#include "read.h"
#include "object.h"
#include "runtime.h"

struct object_t empty_pair_storage = { { TAG_PAIR, 0, 0 }, { 0 } };
struct object_t *empty_pair = &empty_pair_storage;

void
environment_initialize(struct environment_t *environment)
{
    struct special_function_initializer_t
    {
        const char *name;
        special_function_t function;
    };

    static struct special_function_initializer_t initializers[] = 
    {
        { "quote", quote },
        { "read", read },
        { "eval", eval },
        { "print", print },
        { "set!", set },
        { "cons", cons },
        { "car", car },
        { "cdr", cdr },
        { "define", define },
        { "lambda", lambda },
        { "vector", vector },
    };
    #define NUM_INITIALIZERS (sizeof initializers / sizeof initializers[0])
    size_t i;

    for (i = 0; i < NUM_INITIALIZERS; ++i)
    {
        size_t name_length = strlen(initializers[i].name);
        struct object_t *p0 = gc_alloc(environment->heap, TAG_PAIR, 0);
        struct object_t *symbol = gc_alloc(environment->heap, TAG_SYMBOL, name_length);
        struct object_t *function = gc_alloc(environment->heap, TAG_SPECIAL_FUNCTION, 0);
        struct object_t **place;
        memmove(symbol->value.string_value, initializers[i].name, name_length);

        CAR(p0) = symbol;
        function->value.special_function_value = initializers[i].function;
        place = bind(environment, p0);
        *place = function;
    }
}

struct environment_t *
environment_create(void *stack, size_t stack_size, void *heap_mem, size_t heap_size)
{
    struct heap_t *heap = gc_create(heap_mem, heap_size);
    struct environment_t *env = gc_alloc(heap, TAG_ENVIRONMENT, 0);

    env->stack_bottom = stack;
    env->stack_top = (char *)stack + stack_size;
    env->stack_ptr = env->stack_top;
    env->heap = heap;
    env->symbol_table_fragment = NULL;

    environment_initialize(env);
    return env;
}

