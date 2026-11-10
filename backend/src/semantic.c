#include "semantic.h"
#include "symbol_table.h"

#include <stdio.h>

static SymbolTable symbols;

static void check_expression(const ASTNode *node, DiagnosticList *diags);
static void check_statement(const ASTNode *node, DiagnosticList *diags);

static void report_error(DiagnosticList *diags, const ASTNode *node,
                         const char *message, const char *name)
{
    diag_list_add(diags, DIAG_STAGE_SEMANTIC, DIAG_ERROR,
                  node->line, node->column, message, name);
}

static void check_expression(const ASTNode *node, DiagnosticList *diags)
{
    if (!node) return;

    switch (node->type) {
        case AST_INTEGER:
            break;

        case AST_IDENTIFIER:
            if (!symbol_table_lookup(&symbols, node->data.identifier.name)) {
                report_error(diags, node,
                             "Variable '%s' is not declared",
                             node->data.identifier.name);
            }
            break;

        case AST_BINARY_EXPR:
            check_expression(node->data.binary.left, diags);
            check_expression(node->data.binary.right, diags);
            break;

        case AST_UNARY_EXPR:
            check_expression(node->data.unary.operand, diags);
            break;

        default:
            break;
    }
}

static void check_statement(const ASTNode *node, DiagnosticList *diags)
{
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
            for (int i = 0; i < node->data.program.count; i++) {
                check_statement(node->data.program.statements[i], diags);
            }
            break;

        case AST_DECLARATION: {
            const char *name = node->data.declaration.name;
            const char *type = node->data.declaration.data_type;

            if (symbol_table_exists_in_current_scope(&symbols, name)) {
                report_error(diags, node,
                             "Variable '%s' is already declared in this scope",
                             name);
                break;
            }

            if (!symbol_table_insert(&symbols, name, type)) {
                diag_list_add(diags, DIAG_STAGE_SEMANTIC, DIAG_ERROR,
                              node->line, node->column,
                              "Could not insert variable '%s'", name);
                break;
            }

            if (node->data.declaration.initializer) {
                check_expression(node->data.declaration.initializer, diags);
            }
            break;
        }

        case AST_ASSIGNMENT: {
            const char *name = node->data.assignment.name;

            if (!symbol_table_lookup(&symbols, name)) {
                report_error(diags, node,
                             "Variable '%s' is not declared", name);
            }

            check_expression(node->data.assignment.value, diags);
            break;
        }

        case AST_IF:
            check_expression(node->data.if_statement.condition, diags);
            check_statement(node->data.if_statement.then_branch, diags);
            check_statement(node->data.if_statement.else_branch, diags);
            break;

        case AST_WHILE:
            check_expression(node->data.while_statement.condition, diags);
            check_statement(node->data.while_statement.body, diags);
            break;

        case AST_BLOCK:
            symbol_table_enter_scope(&symbols);

            for (int i = 0; i < node->data.block.count; i++) {
                check_statement(node->data.block.statements[i], diags);
            }

            symbol_table_exit_scope(&symbols);
            break;

        case AST_BINARY_EXPR:
        case AST_UNARY_EXPR:
        case AST_INTEGER:
        case AST_IDENTIFIER:
            check_expression(node, diags);
            break;

        default:
            break;
    }
}

int semantic_analyze(const ASTNode *root, DiagnosticList *diags)
{
    if (!root || !diags) return 0;

    int previous_errors = diag_list_error_count(diags);

    symbol_table_init(&symbols);
    check_statement(root, diags);
    symbol_table_free(&symbols);

    return diag_list_error_count(diags) == previous_errors;
}