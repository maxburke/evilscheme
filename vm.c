#include <assert.h>
#include <string.h>

#include "base.h"
#include "environment.h"
#include "object.h"
#include "runtime.h"
#include "vm.h"

#define ENABLE_VM_ASSERTS 1

#if ENABLE_VM_ASSERTS
    #define VM_ASSERT(x) if (!(x)) { fprintf(stderr, "%s:%d: Assertion failed: %s", __FILE__, __LINE__, #x); BREAK(); } else (void)0
#else
    #define VM_ASSERT(x)
#endif

#define STACK_PUSH(stack, x) do { *(--stack) = x; } while (0)
#define STACK_POP(stack) *(stack++) 
#ifndef NDEBUG
#   define VALIDATE_STACK(env) do { \
        assert(env->stack_ptr >= env->stack_bottom && env->stack_ptr < env->stack_top); } while(0)
#else
#   define VALIDATE_STACK(env)
#endif

#define LDIMM_1_IMPL(FIELD, TYPE, TAG) {        \
        sp->tag_count.tag = TAG;                \
        sp->tag_count.flag = 0;                 \
        sp->tag_count.count = 1;                \
        sp->value.FIELD = (TYPE)(*pc++);        \
        --sp;                                   \
    }

#define LDIMM_1_BOOLEAN()   LDIMM_1_IMPL(fixnum_value, int64_t, TAG_BOOLEAN)
#define LDIMM_1_CHAR()      LDIMM_1_IMPL(fixnum_value, int64_t, TAG_CHAR)
#define LDIMM_1_FIXNUM()    LDIMM_1_IMPL(fixnum_value, int64_t, TAG_FIXNUM)
#define LDIMM_1_FLONUM()    LDIMM_1_IMPL(flonum_value, double, TAG_FLONUM)

union four_byte_union_t
{
    unsigned char bytes[4];
    int fixnum_value;
    float flonum_value;
};

#define LDIMM_4_IMPL(TAG, FIELD) {                                                          \
        union four_byte_union_t fb;                                                         \
        sp->tag_count.tag = TAG;                                                            \
        sp->tag_count.flag = 0;                                                             \
        sp->tag_count.count = 1;                                                            \
        fb.bytes[0] = *pc++; fb.bytes[1] = *pc++; fb.bytes[2] = *pc++; fb.bytes[3] = *pc++; \
        sp->value.FIELD = fb.FIELD;                                                         \
    }

#define LDIMM_4_FIXNUM() LDIMM_4_IMPL(TAG_FIXNUM, fixnum_value)
#define LDIMM_4_FLONUM() LDIMM_4_IMPL(TAG_FLONUM, flonum_value)

union eight_byte_union_t
{
    unsigned char bytes[8];
    int64_t fixnum_value;
    double flonum_value;
    uint64_t symbol_hash;
};

#define LDIMM_8_IMPL(TAG, FIELD) {                                                          \
        union eight_byte_union_t fb;                                                        \
        sp->tag_count.tag = TAG;                                                            \
        sp->tag_count.flag = 0;                                                             \
        sp->tag_count.count = 1;                                                            \
        fb.bytes[0] = *pc++; fb.bytes[1] = *pc++; fb.bytes[2] = *pc++; fb.bytes[3] = *pc++; \
        fb.bytes[4] = *pc++; fb.bytes[5] = *pc++; fb.bytes[6] = *pc++; fb.bytes[7] = *pc++; \
        sp->value.FIELD = fb.FIELD;                                                         \
    }

#define LDIMM_8_FIXNUM() LDIMM_8_IMPL(TAG_FIXNUM, fixnum_value)
#define LDIMM_8_FLONUM() LDIMM_8_IMPL(TAG_FLONUM, flonum_value)
#define LDIMM_8_SYMBOL() LDIMM_8_IMPL(TAG_SYMBOL, symbol_hash)

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
                {
                    unsigned char arg_index = *pc++;
                    STACK_PUSH(sp, program_area[arg_index]);
                }
                break;
            case OPCODE_LDIMM_1_BOOL:
                LDIMM_1_BOOLEAN()
                break;
            case OPCODE_LDIMM_1_CHAR:
                LDIMM_1_CHAR()
                break;
            case OPCODE_LDIMM_1_FIXNUM:
                LDIMM_1_FIXNUM()
                break;
            case OPCODE_LDIMM_1_FLONUM:
                LDIMM_1_FLONUM()
                break;
            case OPCODE_LDIMM_4_FIXNUM:
                LDIMM_4_FIXNUM()
                break;
            case OPCODE_LDIMM_4_FLONUM:
                LDIMM_4_FLONUM()
                break;
            case OPCODE_LDIMM_8_FIXNUM:
                LDIMM_8_FIXNUM()
                break;
            case OPCODE_LDIMM_8_FLONUM:
                LDIMM_8_FLONUM()
                break;
            case OPCODE_LDIMM_8_SYMBOL:
                LDIMM_8_SYMBOL()
                break;
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

