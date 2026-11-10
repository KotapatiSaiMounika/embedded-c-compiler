
#include "optimizer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef struct {
    char *name;
    int value;
    int valid;
} Constant;

static char *copy_string(const char *s)
{
    if (!s) return NULL;

    size_t length = strlen(s) + 1;
    char *copy = malloc(length);

    if (copy)
        memcpy(copy, s, length);

    return copy;
}

static int is_number(const char *text, int *value)
{
    if (!text || !*text) return 0;

    char *end;
    long number = strtol(text, &end, 10);

    if (*end != '\0' || number < INT_MIN || number > INT_MAX)
        return 0;

    *value = (int)number;
    return 1;
}

static int is_temporary(const char *name)
{
    if (!name || name[0] != 't' || name[1] == '\0')
        return 0;

    for (int i = 1; name[i]; i++) {
        if (name[i] < '0' || name[i] > '9')
            return 0;
    }

    return 1;
}

static void clear_constants(Constant *constants, int count)
{
    for (int i = 0; i < count; i++)
        constants[i].valid = 0;
}

static int find_constant(Constant *constants, int count,
                         const char *name)
{
    if (!name) return -1;

    for (int i = 0; i < count; i++) {
        if (constants[i].valid &&
            strcmp(constants[i].name, name) == 0)
            return i;
    }

    return -1;
}

static void set_constant(Constant *constants, int count,
                          const char *name, int value)
{
    if (!name) return;

    int index = find_constant(constants, count, name);

    if (index >= 0) {
        constants[index].value = value;
        return;
    }

    for (int i = 0; i < count; i++) {
        if (!constants[i].valid) {
            char *copy = copy_string(name);

            if (!copy) return;

            free(constants[i].name);
            constants[i].name = copy;
            constants[i].value = value;
            constants[i].valid = 1;
            return;
        }
    }
}

static void invalidate_constant(Constant *constants, int count,
                                const char *name)
{
    int index = find_constant(constants, count, name);

    if (index >= 0)
        constants[index].valid = 0;
}

static void replace_string(char **target, const char *value)
{
    if (!target || !value) return;

    char *copy = copy_string(value);

    if (!copy) return;

    free(*target);
    *target = copy;
}

static void propagate_operand(char **operand,
                              Constant *constants,
                              int count)
{
    if (!operand || !*operand) return;

    int index = find_constant(constants, count, *operand);

    if (index >= 0) {
        char buffer[32];

        snprintf(buffer, sizeof(buffer), "%d",
                 constants[index].value);

        replace_string(operand, buffer);
    }
}

static int calculate_binary(const char *op, int a, int b, int *result)
{
    long long value;

    if (strcmp(op, "+") == 0)
        value = (long long)a + b;
    else if (strcmp(op, "-") == 0)
        value = (long long)a - b;
    else if (strcmp(op, "*") == 0)
        value = (long long)a * b;
    else if (strcmp(op, "/") == 0) {
        if (b == 0) return 0;
        if (a == INT_MIN && b == -1) return 0;
        value = a / b;
    }
    else if (strcmp(op, "<") == 0)
        value = a < b;
    else if (strcmp(op, ">") == 0)
        value = a > b;
    else if (strcmp(op, "<=") == 0)
        value = a <= b;
    else if (strcmp(op, ">=") == 0)
        value = a >= b;
    else if (strcmp(op, "==") == 0)
        value = a == b;
    else if (strcmp(op, "!=") == 0)
        value = a != b;
    else if (strcmp(op, "&&") == 0)
        value = (a != 0 && b != 0);
    else if (strcmp(op, "||") == 0)
        value = (a != 0 || b != 0);
    else
        return 0;

    if (value < INT_MIN || value > INT_MAX)
        return 0;

    *result = (int)value;
    return 1;
}

static int calculate_unary(const char *op, int a, int *result)
{
    if (strcmp(op, "-") == 0) {
        if (a == INT_MIN) return 0;
        *result = -a;
        return 1;
    }

    if (strcmp(op, "!") == 0) {
        *result = !a;
        return 1;
    }

    return 0;
}

/* Constant propagation and constant folding */
static int optimize_constants(IRProgram *ir,
                              Constant *constants,
                              int constant_count)
{
    int changes = 0;

    for (int i = 0; i < ir->count; i++) {
        IRInstruction *instruction = &ir->instructions[i];

        if (!instruction->op)
            continue;

        /* Avoid carrying constants across control-flow boundaries. */
        if (strcmp(instruction->op, "label") == 0 ||
            strcmp(instruction->op, "goto") == 0 ||
            strcmp(instruction->op, "ifFalse") == 0) {

            if (strcmp(instruction->op, "ifFalse") == 0)
                propagate_operand(&instruction->arg1,
                                  constants, constant_count);

            clear_constants(constants, constant_count);
            continue;
        }

        propagate_operand(&instruction->arg1,
                          constants, constant_count);

        propagate_operand(&instruction->arg2,
                          constants, constant_count);

        if (strcmp(instruction->op, "=") == 0) {
            int value;

            if (is_number(instruction->arg1, &value)) {
                set_constant(constants, constant_count,
                             instruction->result, value);
            } else {
                invalidate_constant(constants, constant_count,
                                    instruction->result);
            }

            continue;
        }

        if (!instruction->result)
            continue;

        int a, b, result;
        int folded = 0;

        if (instruction->arg1 && instruction->arg2 &&
            is_number(instruction->arg1, &a) &&
            is_number(instruction->arg2, &b)) {

            folded = calculate_binary(instruction->op,
                                      a, b, &result);
        }
        else if (instruction->arg1 && !instruction->arg2 &&
                 is_number(instruction->arg1, &a)) {

            folded = calculate_unary(instruction->op,
                                     a, &result);
        }

        if (folded) {
            char buffer[32];

            snprintf(buffer, sizeof(buffer), "%d", result);

            replace_string(&instruction->op, "=");
            replace_string(&instruction->arg1, buffer);

            free(instruction->arg2);
            instruction->arg2 = NULL;

            set_constant(constants, constant_count,
                         instruction->result, result);

            changes++;
        } else {
            invalidate_constant(constants, constant_count,
                                instruction->result);
        }
    }

    return changes;
}

static int operand_uses(const IRInstruction *instruction,
                        const char *name)
{
    if (!instruction || !name) return 0;

    int uses = 0;

    if (instruction->arg1 &&
        strcmp(instruction->arg1, name) == 0)
        uses++;

    if (instruction->arg2 &&
        strcmp(instruction->arg2, name) == 0)
        uses++;

    return uses;
}

static int count_uses(const IRProgram *ir, const char *name)
{
    int uses = 0;

    for (int i = 0; i < ir->count; i++)
        uses += operand_uses(&ir->instructions[i], name);

    return uses;
}

static void remove_instruction(IRProgram *ir, int index)
{
    free(ir->instructions[index].op);
    free(ir->instructions[index].arg1);
    free(ir->instructions[index].arg2);
    free(ir->instructions[index].result);

    for (int i = index; i < ir->count - 1; i++)
        ir->instructions[i] = ir->instructions[i + 1];

    ir->count--;

    if (ir->count >= 0)
        memset(&ir->instructions[ir->count], 0,
               sizeof(IRInstruction));
}

/* Dead code elimination */
static int eliminate_dead_code(IRProgram *ir)
{
    int removed = 0;
    int changed;

    do {
        changed = 0;

        for (int i = 0; i < ir->count; i++) {
            IRInstruction *instruction = &ir->instructions[i];

            if (!instruction->op ||
                !instruction->result ||
                !is_temporary(instruction->result))
                continue;

            if (strcmp(instruction->op, "label") == 0 ||
                strcmp(instruction->op, "goto") == 0 ||
                strcmp(instruction->op, "ifFalse") == 0)
                continue;

            if (count_uses(ir, instruction->result) == 0) {
                remove_instruction(ir, i);
                removed++;
                changed = 1;
                break;
            }
        }
    } while (changed);

    return removed;
}

int ir_optimize(IRProgram *ir, DiagnosticList *diags)
{
    if (!ir || !diags)
        return 0;

    if (ir->count == 0)
        return 1;

    int capacity = ir->count * 2 + 1;

    Constant *constants =
        calloc((size_t)capacity, sizeof(Constant));

    if (!constants) {
        diag_list_add(diags, DIAG_STAGE_OPTIMIZER, DIAG_ERROR,
                      0, 0, "Memory allocation failed in optimizer");
        return 0;
    }

    int changes = optimize_constants(ir, constants, capacity);

    for (int i = 0; i < capacity; i++)
        free(constants[i].name);

    free(constants);

    changes += eliminate_dead_code(ir);

    (void)changes;

    return 1;
}
