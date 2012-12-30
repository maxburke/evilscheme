#include <assert.h>
#include <string.h>

#include "base.h"
#include "environment.h"
#include "object.h"
#include "runtime.h"
#include "vm.h"

static int
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

static inline void
push_ref(struct environment_t *environment, void *ref)
{
    struct object_t return_address;

    return_address.tag_count.tag = TAG_REFERENCE;
    return_address.tag_count.flag = 0;
    return_address.tag_count.count = 1;
    return_address.value.ref = ref;

    *(environment->stack_ptr) = return_address;
}

struct object_t *
vm_run(struct environment_t *environment, struct object_t *fn, struct object_t *args)
{
    struct procedure_t *procedure;
    struct object_t *program_area;
    struct object_t *sp;
    struct object_t *old_stack;
    int num_args;
    unsigned char *pc;
   
    procedure = (struct procedure_t *)fn;
    old_stack = environment->stack_ptr;
    sp = old_stack;
    program_area = old_stack;
    pc = procedure->byte_code;

    /*
     * Stack layout for VM:
     * [high addresses]............................................................................[low addresses]
     * [arg n - 1][...][arg 1][arg 0][return address][program area chain][stack chain][stack top]...[stack_bottom]
     *                                      ^                                               ^
     *                  program_area -------+                                     sp -------+
     *
     * arg0 is at program_area[1]
     */

    num_args = push_args_to_stack(environment, args);
    assert(num_args == procedure->num_args);
    push_ref(environment, NULL);    /* return address */
    push_ref(environment, NULL);    /* program area chain */
    push_ref(environment, NULL);    /* stack chain */
    sp = environment->stack_ptr;
    fn = environment->stack_ptr;
    
    for (;;)
    {
        unsigned byte = *pc++;

        switch (byte)
        {
            case OPCODE_LDARG_X:
            case OPCODE_LDIMM_1_BOOL:
            case OPCODE_LDIMM_1_CHAR:
            case OPCODE_LDIMM_1_FIXNUM:
            case OPCODE_LDIMM_1_FLONUM:
            case OPCODE_LDIMM_4_FIXNUM:
            case OPCODE_LDIMM_4_FLONUM:
            case OPCODE_LDIMM_8_FIXNUM:
            case OPCODE_LDIMM_8_FLONUM:
            case OPCODE_LDIMM_8_SYMBOL:
            case OPCODE_LDSTR:
            case OPCODE_SET:
            case OPCODE_NEW:
            case OPCODE_NEW_VECTOR:
            case OPCODE_CMP:
            case OPCODE_BRANCH_1:
            case OPCODE_BRANCH_2:
            case OPCODE_COND_BRANCH_1:
            case OPCODE_COND_BRANCH_2:
            case OPCODE_CALL:
            case OPCODE_TAILCALL:
            case OPCODE_RETURN:
            case OPCODE_GET_BOUND_LOCATION:
            case OPCODE_ADD:
            case OPCODE_SUB:
            case OPCODE_MUL:
            case OPCODE_DIV:
            case OPCODE_DUP_X:
            case OPCODE_POP_X:
            case OPCODE_SWAP_X:
            default:
                BREAK();
                break;
        }
    }

    environment->stack_ptr = old_stack;

    /*
     * This should probably cons the last return value on the stack and
     * return that instead.
     */
    return NULL;
}

