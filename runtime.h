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
    struct tag_count_t tag_count;
    
    /*
     * These fields hold the saved VM state in case execution is interrupted
     * or postponed.
     */
    struct object_t *stack_top;
    struct object_t *stack_bottom;
    struct object_t *stack_ptr;
    unsigned int condition_flags;

    struct heap_t *heap;
    struct environment_t *parent_environment;
    struct symbol_table_fragment_t *symbol_table_fragment;
};

struct environment_t *
environment_create(void *stack, size_t stack_size, void *heap, size_t heap_size);

/*
 * C-environment interop functions
 */

#endif
