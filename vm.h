#ifndef VM_H
#define VM_H

/*
 * VM design:
 * Stack based in lieu of data registers. VM contains a condition register
 * that is used for the results of test conditions. 
 */

enum opcode_t
{
    OPCODE_PUSH,
    OPCODE_POP,
    OPCODE_GET,
    OPCODE_SET,
    OPCODE_GET_BOUND_LOCATION,
    OPCODE_BRANCH,
    OPCODE_COND_BRANCH,
    OPCODE_CMP,
    OPCODE_NEW_CELL,
    OPCODE_CALL /* ??? */
};

enum condition_t
{
    CONDITION_EQ,
    CONDITION_LT,
    CONDITION_GT,
    CONDITION_NEQ,
    CONDITION_LE,
    CONDITION_GE
};

struct object_t *
vm_run(struct environment_t *environment, struct object_t *args);

#endif

