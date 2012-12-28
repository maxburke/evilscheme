#include <assert.h>
#include <string.h>

#include "base.h"
#include "environment.h"
#include "object.h"
#include "runtime.h"
#include "vm.h"

extern struct object_t *empty_pair;

int
push_args_to_stack(struct environment_t *environment, struct object_t *args)
{
    struct object_t *i;
    struct object_t *stack_ptr;
    struct object_t *ptr;
    int count;

    /*
     * push_args_to_stack pushes the args in forward order to the bottom of
     * the stack and then memcpy's them to where they need to be. This allows
     * them to be in the correct order without having to do some complex
     * push + reversal.
     */
    
    stack_ptr = environment->stack_ptr;
    ptr = environment->stack_bottom;
    count = 0;

    for (i = args; CAR(i) != empty_pair; i = CDR(i))
    {
        struct object_t *obj;
        
        obj = CAR(i);
        assert(obj->tag_count.count == 1);
        assert(ptr < stack_ptr);
        *ptr++ = *obj;
        ++count;
    }

    stack_ptr -= count;

    memcpy(stack_ptr,
            environment->stack_bottom,
            count * sizeof(struct object_t));

    memset(environment->stack_bottom,
            0,
            count * sizeof(struct object_t));

    environment->stack_ptr = stack_ptr;

    return count;
}

struct object_t *
vm_run(struct environment_t *environment, struct object_t *fn, struct object_t *args)
{
    void *fn_mem;
    struct procedure_t *procedure;
    void *old_stack;
    int num_args;
    char *pc;

    /*
     * Working around initialization-via-unrelated-type-cast warning.
     */
    fn_mem = fn;
    procedure = fn_mem;
    pc = NULL;

    old_stack = environment->stack_ptr;

    num_args = push_args_to_stack(environment, args);
    assert(num_args == procedure->num_args);

    for (;;)
    {
        switch (*pc)
        {
            case OPCODE_LDARG:
            case OPCODE_LDIMM_1:
            case OPCODE_LDIMM_4:
            case OPCODE_LDIMM_8:
            case OPCODE_LDSTR:
            case OPCODE_SET:
            case OPCODE_NEWOBJ:
            case OPCODE_CMP:
            case OPCODE_BRANCH:
            case OPCODE_COND_BRANCH:
            case OPCODE_CALL:
            case OPCODE_GET_BOUND_LOCATION:
            case OPCODE_ADD:
            case OPCODE_SUB:
            case OPCODE_MUL:
            case OPCODE_DIV:
            default:
                BREAK();
                break;
        }

        if (pc == NULL)
            break;
    }

    environment->stack_ptr = old_stack;

    return NULL;
}

