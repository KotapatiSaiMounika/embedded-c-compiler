#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *copy_string(const char *s)
{
    if (!s) return NULL;

    size_t len = strlen(s) + 1;
    char *copy = malloc(len);

    if (copy) memcpy(copy, s, len);
    return copy;
}

void ir_init(IRProgram *ir)
{
    ir->instructions = NULL;
    ir->count = 0;
    ir->capacity = 0;
    ir->temp_counter = 0;
    ir->label_counter = 0;
}

void ir_free(IRProgram *ir)
{
    if (!ir) return;

    for (int i = 0; i < ir->count; i++) {
        free(ir->instructions[i].op);
        free(ir->instructions[i].arg1);
        free(ir->instructions[i].arg2);
        free(ir->instructions[i].result);
    }

    free(ir->instructions);
    ir_init(ir);
}

static int emit(IRProgram *ir, const char *op,
                const char *arg1, const char *arg2,
                const char *result, int line)
{
    if (ir->count == ir->capacity) {
        int new_capacity = ir->capacity ? ir->capacity * 2 : 16;

        IRInstruction *new_items =
            realloc(ir->instructions,
                    (size_t)new_capacity * sizeof(IRInstruction));

        if (!new_items) return 0;

        ir->instructions = new_items;
        ir->capacity = new_capacity;
    }

    IRInstruction instruction = {0};

    instruction.op = copy_string(op);
    instruction.arg1 = copy_string(arg1);
    instruction.arg2 = copy_string(arg2);
    instruction.result = copy_string(result);
    instruction.line = line;

    if (!instruction.op ||
        (arg1 && !instruction.arg1) ||
        (arg2 && !instruction.arg2) ||
        (result && !instruction.result)) {
        free(instruction.op);
        free(instruction.arg1);
        free(instruction.arg2);
        free(instruction.result);
        return 0;
    }

    ir->instructions[ir->count++] = instruction;
    return 1;
}

static char *new_temp(IRProgram *ir)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "t%d", ir->temp_counter++);
    return copy_string(buffer);
}

static char *new_label(IRProgram *ir)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "L%d", ir->label_counter++);
    return copy_string(buffer);
}

static char *generate_expression(const ASTNode *node,
                                 IRProgram *ir,
                                 DiagnosticList *diags)
{
    if (!node) return NULL;

    char buffer[64];

    switch (node->type) {
        case AST_INTEGER:
            snprintf(buffer, sizeof(buffer), "%d",
                     node->data.integer.value);
            return copy_string(buffer);

        case AST_IDENTIFIER:
            return copy_string(node->data.identifier.name);

        case AST_BINARY_EXPR: {
            char *left = generate_expression(
                node->data.binary.left, ir, diags);

            char *right = generate_expression(
                node->data.binary.right, ir, diags);

            if (!left || !right) {
                free(left);
                free(right);
                return NULL;
            }

            char *temp = new_temp(ir);

            if (!temp ||
                !emit(ir, node->data.binary.operator,
                      left, right, temp, node->line)) {
                free(left);
                free(right);
                free(temp);
                goto memory_error;
            }

            free(left);
            free(right);
            return temp;
        }

        case AST_UNARY_EXPR: {
            char *operand = generate_expression(
                node->data.unary.operand, ir, diags);

            if (!operand) return NULL;

            char *temp = new_temp(ir);

            if (!temp ||
                !emit(ir, node->data.unary.operator,
                      operand, NULL, temp, node->line)) {
                free(operand);
                free(temp);
                goto memory_error;
            }

            free(operand);
            return temp;
        }

        default:
            diag_list_add(diags, DIAG_STAGE_IR, DIAG_ERROR,
                          node->line, node->column,
                          "Unsupported expression in IR generation");
            return NULL;
    }

memory_error:
    diag_list_add(diags, DIAG_STAGE_IR, DIAG_ERROR,
                  node->line, node->column,
                  "Memory allocation failed during IR generation");
    return NULL;
}

static int generate_statement(const ASTNode *node,
                              IRProgram *ir,
                              DiagnosticList *diags)
{
    if (!node) return 1;

    switch (node->type) {
        case AST_PROGRAM:
            for (int i = 0; i < node->data.program.count; i++) {
                if (!generate_statement(
                        node->data.program.statements[i], ir, diags))
                    return 0;
            }
            return 1;

        case AST_DECLARATION: {
            if (!node->data.declaration.initializer)
                return 1;

            char *value = generate_expression(
                node->data.declaration.initializer, ir, diags);

            if (!value) return 0;

            int ok = emit(ir, "=", value, NULL,
                          node->data.declaration.name, node->line);

            free(value);
            return ok;
        }

        case AST_ASSIGNMENT: {
            char *value = generate_expression(
                node->data.assignment.value, ir, diags);

            if (!value) return 0;

            int ok = emit(ir, "=", value, NULL,
                          node->data.assignment.name, node->line);

            free(value);
            return ok;
        }

        case AST_BLOCK:
            for (int i = 0; i < node->data.block.count; i++) {
                if (!generate_statement(
                        node->data.block.statements[i], ir, diags))
                    return 0;
            }
            return 1;

        case AST_IF: {
            char *condition = generate_expression(
                node->data.if_statement.condition, ir, diags);

            char *else_label = new_label(ir);
            char *end_label = new_label(ir);

            if (!condition || !else_label || !end_label)
                goto if_error;

            if (!emit(ir, "ifFalse", condition, NULL,
                      else_label, node->line))
                goto if_error;

            if (!generate_statement(
                    node->data.if_statement.then_branch, ir, diags))
                goto if_error;

            if (node->data.if_statement.else_branch) {
                if (!emit(ir, "goto", NULL, NULL,
                          end_label, node->line))
                    goto if_error;
            }

            if (!emit(ir, "label", NULL, NULL,
                      else_label, node->line))
                goto if_error;

            if (node->data.if_statement.else_branch) {
                if (!generate_statement(
                        node->data.if_statement.else_branch, ir, diags))
                    goto if_error;

                if (!emit(ir, "label", NULL, NULL,
                          end_label, node->line))
                    goto if_error;
            }

            free(condition);
            free(else_label);
            free(end_label);
            return 1;

if_error:
            free(condition);
            free(else_label);
            free(end_label);
            return 0;
        }

        case AST_WHILE: {
            char *start_label = new_label(ir);
            char *end_label = new_label(ir);

            if (!start_label || !end_label) {
                free(start_label);
                free(end_label);
                return 0;
            }

            if (!emit(ir, "label", NULL, NULL,
                      start_label, node->line))
                goto while_error;

            char *condition = generate_expression(
                node->data.while_statement.condition, ir, diags);

            if (!condition) goto while_error;

            if (!emit(ir, "ifFalse", condition, NULL,
                      end_label, node->line)) {
                free(condition);
                goto while_error;
            }

            free(condition);

            if (!generate_statement(
                    node->data.while_statement.body, ir, diags))
                goto while_error;

            if (!emit(ir, "goto", NULL, NULL,
                      start_label, node->line))
                goto while_error;

            if (!emit(ir, "label", NULL, NULL,
                      end_label, node->line))
                goto while_error;

            free(start_label);
            free(end_label);
            return 1;

while_error:
            free(start_label);
            free(end_label);
            return 0;
        }

        default:
            diag_list_add(diags, DIAG_STAGE_IR, DIAG_ERROR,
                          node->line, node->column,
                          "Unsupported statement in IR generation");
            return 0;
    }
}

int ir_generate(const ASTNode *root,
                IRProgram *ir,
                DiagnosticList *diags)
{
    if (!root || !ir || !diags) return 0;

    int previous_errors = diag_list_error_count(diags);

    if (!generate_statement(root, ir, diags)) {
        if (diag_list_error_count(diags) == previous_errors) {
            diag_list_add(diags, DIAG_STAGE_IR, DIAG_ERROR,
                          0, 0,
                          "Failed to generate intermediate representation");
        }
        return 0;
    }

    return diag_list_error_count(diags) == previous_errors;
}

void ir_print(const IRProgram *ir)
{
    if (!ir) return;

    for (int i = 0; i < ir->count; i++) {
        const IRInstruction *instruction = &ir->instructions[i];

        printf("%s", instruction->op);

        if (instruction->arg1)
            printf(" %s", instruction->arg1);

        if (instruction->arg2)
            printf(", %s", instruction->arg2);

        if (instruction->result)
            printf(" -> %s", instruction->result);

        printf("\n");
    }
}