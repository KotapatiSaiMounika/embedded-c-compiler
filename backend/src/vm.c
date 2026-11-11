#include "vm.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ program */

void vm_program_init(VMProgram *p)
{
    memset(p, 0, sizeof(*p));
}

void vm_program_free(VMProgram *p)
{
    if (!p) return;
    for (int i = 0; i < p->var_count; i++) free(p->vars[i].name);
    free(p->vars);
    free(p->code);
    vm_program_init(p);
}

static int is_temp_name(const char *name)
{
    if (name[0] != 't' || name[1] == '\0') return 0;
    for (int i = 1; name[i]; i++)
        if (name[i] < '0' || name[i] > '9') return 0;
    return 1;
}

int vm_program_slot(VMProgram *p, const char *name)
{
    for (int i = 0; i < p->var_count; i++)
        if (strcmp(p->vars[i].name, name) == 0) return i;

    if (p->var_count == p->var_capacity) {
        int cap = p->var_capacity ? p->var_capacity * 2 : 16;
        VMVariable *items = realloc(p->vars, (size_t)cap * sizeof(VMVariable));
        if (!items) return -1;
        p->vars = items;
        p->var_capacity = cap;
    }

    size_t len = strlen(name) + 1;
    char *copy = malloc(len);
    if (!copy) return -1;
    memcpy(copy, name, len);

    p->vars[p->var_count].name = copy;
    p->vars[p->var_count].is_temp = is_temp_name(name);
    return p->var_count++;
}

int vm_program_emit(VMProgram *p, OpCode op, int32_t operand, int line)
{
    if (p->code_count == p->code_capacity) {
        int cap = p->code_capacity ? p->code_capacity * 2 : 32;
        VMInstruction *items = realloc(p->code, (size_t)cap * sizeof(VMInstruction));
        if (!items) return -1;
        p->code = items;
        p->code_capacity = cap;
    }
    p->code[p->code_count].op = op;
    p->code[p->code_count].operand = operand;
    p->code[p->code_count].line = line;
    return p->code_count++;
}

/* ------------------------------------------------------------- disassembler */

static const char *const OP_NAMES[] = {
    "PUSH", "LOAD", "STORE", "ADD", "SUB", "MUL", "DIV",
    "LT", "GT", "LE", "GE", "EQ", "NE", "AND", "OR",
    "NEG", "NOT", "JMP", "JZ", "HALT"
};

const char *vm_opcode_name(OpCode op)
{
    if ((int)op < 0 || (size_t)op >= sizeof(OP_NAMES) / sizeof(OP_NAMES[0])) return "???";
    return OP_NAMES[op];
}

int vm_opcode_has_operand(OpCode op)
{
    return op == OP_PUSH || op == OP_LOAD || op == OP_STORE || op == OP_JMP || op == OP_JZ;
}

void vm_format_instruction(const VMProgram *p, int address, char *buf, size_t size)
{
    if (address < 0 || address >= p->code_count) {
        snprintf(buf, size, "???");
        return;
    }
    const VMInstruction *in = &p->code[address];
    if (in->op == OP_LOAD || in->op == OP_STORE) {
        const char *name = (in->operand >= 0 && in->operand < p->var_count)
                               ? p->vars[in->operand].name : "?";
        snprintf(buf, size, "%s %s", vm_opcode_name(in->op), name);
    } else if (vm_opcode_has_operand(in->op)) {
        snprintf(buf, size, "%s %d", vm_opcode_name(in->op), (int)in->operand);
    } else {
        snprintf(buf, size, "%s", vm_opcode_name(in->op));
    }
}

/* ---------------------------------------------------------------- execution */

int vm_init(VM *vm, const VMProgram *p)
{
    memset(vm, 0, sizeof(*vm));
    vm->program = p;
    vm->status = VM_RUNNING;
    vm->last_pc = -1;
    vm->memory = calloc(p->var_count > 0 ? (size_t)p->var_count : 1, sizeof(int32_t));
    return vm->memory != NULL;
}

void vm_free(VM *vm)
{
    if (!vm) return;
    free(vm->memory);
    vm->memory = NULL;
}

static VMStatus fail(VM *vm, const char *message)
{
    snprintf(vm->error, sizeof(vm->error), "%s", message);
    vm->status = VM_ERROR;
    return VM_ERROR;
}

static int32_t wrap32(long long value)
{
    return (int32_t)(uint32_t)((unsigned long long)value & 0xFFFFFFFFull);
}

VMStatus vm_step(VM *vm)
{
    if (vm->status != VM_RUNNING) return vm->status;

    const VMProgram *p = vm->program;

    if (vm->pc < 0 || vm->pc >= p->code_count)
        return fail(vm, "Program counter out of range (missing HALT?)");

    const VMInstruction *in = &p->code[vm->pc];
    vm->last_pc = vm->pc;
    vm->last_line = in->line;
    vm->steps++;
    vm->pc++;

    switch (in->op) {
    case OP_PUSH:
        if (vm->sp >= VM_STACK_SIZE) return fail(vm, "Stack overflow");
        vm->stack[vm->sp++] = in->operand;
        break;

    case OP_LOAD:
        if (vm->sp >= VM_STACK_SIZE) return fail(vm, "Stack overflow");
        if (in->operand < 0 || in->operand >= p->var_count) return fail(vm, "Invalid memory address");
        vm->stack[vm->sp++] = vm->memory[in->operand];
        break;

    case OP_STORE:
        if (vm->sp < 1) return fail(vm, "Stack underflow");
        if (in->operand < 0 || in->operand >= p->var_count) return fail(vm, "Invalid memory address");
        vm->memory[in->operand] = vm->stack[--vm->sp];
        break;

    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
    case OP_LT: case OP_GT: case OP_LE: case OP_GE: case OP_EQ: case OP_NE:
    case OP_AND: case OP_OR: {
        if (vm->sp < 2) return fail(vm, "Stack underflow");
        int32_t b = vm->stack[--vm->sp];
        int32_t a = vm->stack[--vm->sp];
        int32_t r = 0;
        switch (in->op) {
        case OP_ADD: r = wrap32((long long)a + b); break;
        case OP_SUB: r = wrap32((long long)a - b); break;
        case OP_MUL: r = wrap32((long long)a * b); break;
        case OP_DIV:
            if (b == 0) return fail(vm, "Division by zero");
            if (a == INT32_MIN && b == -1) return fail(vm, "Integer overflow in division");
            r = a / b;
            break;
        case OP_LT: r = a < b;  break;
        case OP_GT: r = a > b;  break;
        case OP_LE: r = a <= b; break;
        case OP_GE: r = a >= b; break;
        case OP_EQ: r = a == b; break;
        case OP_NE: r = a != b; break;
        case OP_AND: r = (a != 0 && b != 0); break;
        default:     r = (a != 0 || b != 0); break; /* OP_OR */
        }
        vm->stack[vm->sp++] = r;
        break;
    }

    case OP_NEG:
        if (vm->sp < 1) return fail(vm, "Stack underflow");
        vm->stack[vm->sp - 1] = wrap32(-(long long)vm->stack[vm->sp - 1]);
        break;

    case OP_NOT:
        if (vm->sp < 1) return fail(vm, "Stack underflow");
        vm->stack[vm->sp - 1] = !vm->stack[vm->sp - 1];
        break;

    case OP_JMP:
        vm->pc = in->operand;
        break;

    case OP_JZ:
        if (vm->sp < 1) return fail(vm, "Stack underflow");
        if (vm->stack[--vm->sp] == 0) vm->pc = in->operand;
        break;

    case OP_HALT:
        vm->status = VM_HALTED;
        break;

    default:
        return fail(vm, "Unknown instruction");
    }

    return vm->status;
}

VMStatus vm_run(VM *vm, long max_steps)
{
    while (vm->status == VM_RUNNING) {
        if (vm->steps >= max_steps)
            return fail(vm, "Step limit exceeded (possible infinite loop)");
        vm_step(vm);
    }
    return vm->status;
}