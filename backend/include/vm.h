#ifndef VM_H
#define VM_H

/*
 * vm.h - bytecode format and the stack-based virtual machine (Module 3).
 *
 * Memory model : every variable and every compiler temporary gets one slot
 *                (a 32-bit signed int). All slots start at 0.
 * Stack        : evaluation stack of 32-bit ints, VM_STACK_SIZE entries.
 * Arithmetic   : + - * wrap around on overflow (like a 32-bit CPU).
 *                Division by zero and INT_MIN / -1 are runtime errors.
 * Booleans     : comparisons and logic produce 0 or 1; any non-zero is true.
 */

#include <stdint.h>
#include <stddef.h>

#define VM_STACK_SIZE 256
#define VM_DEFAULT_MAX_STEPS 100000L

typedef enum {
    OP_PUSH,   /* push operand (constant)              */
    OP_LOAD,   /* push memory[operand]                 */
    OP_STORE,  /* memory[operand] = pop                */
    OP_ADD, OP_SUB, OP_MUL, OP_DIV,
    OP_LT, OP_GT, OP_LE, OP_GE, OP_EQ, OP_NE,
    OP_AND, OP_OR,
    OP_NEG, OP_NOT,
    OP_JMP,    /* pc = operand                         */
    OP_JZ,     /* pop; if value == 0 then pc = operand */
    OP_HALT
} OpCode;

typedef struct {
    OpCode op;
    int32_t operand; /* constant, slot index or jump target; 0 if unused */
    int line;        /* source line (0 if unknown)                       */
} VMInstruction;

typedef struct {
    char *name;
    int is_temp;     /* 1 for compiler temporaries t0, t1, ... */
} VMVariable;

typedef struct {
    VMInstruction *code;
    int code_count;
    int code_capacity;
    VMVariable *vars;
    int var_count;
    int var_capacity;
} VMProgram;

void vm_program_init(VMProgram *program);
void vm_program_free(VMProgram *program);

/* Returns the slot index of `name`, creating it if needed (-1 on OOM). */
int vm_program_slot(VMProgram *program, const char *name);
/* Appends an instruction. Returns its address, or -1 on OOM. */
int vm_program_emit(VMProgram *program, OpCode op, int32_t operand, int line);

const char *vm_opcode_name(OpCode op);
int vm_opcode_has_operand(OpCode op);
/* Writes e.g. "PUSH 2", "LOAD x", "JZ 12", "ADD" into buf. */
void vm_format_instruction(const VMProgram *program, int address, char *buf, size_t size);

typedef enum {
    VM_RUNNING, /* last step succeeded, more to do */
    VM_HALTED,  /* HALT executed                   */
    VM_ERROR    /* runtime error, see vm->error    */
} VMStatus;

typedef struct {
    const VMProgram *program;
    int32_t stack[VM_STACK_SIZE];
    int sp;                 /* number of values on the stack */
    int32_t *memory;        /* program->var_count slots      */
    int pc;
    long steps;
    VMStatus status;
    char error[128];
    int last_pc;            /* address of the instruction executed by the last step */
    int last_line;          /* its source line (for runtime error positions)        */
} VM;

/* Returns 1 on success, 0 on out-of-memory. */
int vm_init(VM *vm, const VMProgram *program);
void vm_free(VM *vm);

/* Executes exactly one instruction. */
VMStatus vm_step(VM *vm);
/* Runs until HALT / error / max_steps (then an error is set). */
VMStatus vm_run(VM *vm, long max_steps);

#endif /* VM_H */