#ifndef RUNTUME_H
#define RUNTIME_H

#include "object.h"

#define STACK_PUSH(stack, x) do { *(--stack) = x; } while (0)
#define STACK_POP(stack) *(stack++) 
#ifndef NDEBUG
#   define VALIDATE_STACK(env) do { \
        assert(env->stack_ptr >= env->stack_bottom && env->stack_ptr < env->stack_top); } while(0)
#else
#   define VALIDATE_STACK(env)
#endif

struct heap_t;
struct symbol_table_fragment_t;

struct environment_t
{
    void *stack_top;
    void *stack_bottom;
    void *stack_ptr;
    struct heap_t *heap;
    struct symbol_table_fragment_t *symbol_table;
};

struct environment_t *
environment_create(void *stack, size_t stack_size, void *heap, size_t heap_size);

/*
 * C-environment interop functions
 */

#endif
