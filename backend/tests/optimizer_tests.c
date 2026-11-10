#include "optimizer.h"
#include "ir.h"
#include "ast.h"
#include "diagnostics.h"

#include <stdio.h>
#include <string.h>

static int test_constant_folding(void)
{
    IRProgram ir;
    DiagnosticList diagnostics;

    ir_init(&ir);
    diag_list_init(&diagnostics);

    ASTNode *root = ast_new_program(1, 1);

    ASTNode *expression = ast_new_binary(
        "+",
        ast_new_integer(10, 1, 5),
        ast_new_integer(20, 1, 10),
        1, 7
    );

    ASTNode *declaration = ast_new_declaration(
        "a", "int", expression, 1, 1
    );

    ast_append_statement(root, declaration);

    int success = ir_generate(root, &ir, &diagnostics);
    success = success && ir_optimize(&ir, &diagnostics);

    int passed = success &&
        ir.count == 1 &&
        strcmp(ir.instructions[0].op, "=") == 0 &&
        strcmp(ir.instructions[0].arg1, "30") == 0 &&
        strcmp(ir.instructions[0].result, "a") == 0;

    printf("%s: Constant folding\n", passed ? "PASS" : "FAIL");

    ast_free(root);
    ir_free(&ir);
    diag_list_free(&diagnostics);

    return passed;
}

static int test_constant_propagation(void)
{
    IRProgram ir;
    DiagnosticList diagnostics;

    ir_init(&ir);
    diag_list_init(&diagnostics);

    ASTNode *root = ast_new_program(1, 1);

    ast_append_statement(root,
        ast_new_declaration(
            "a", "int", ast_new_integer(10, 1, 5), 1, 1
        )
    );

    ast_append_statement(root,
        ast_new_declaration(
            "b", "int", ast_new_identifier("a", 2, 5), 2, 1
        )
    );

    int success = ir_generate(root, &ir, &diagnostics);
    success = success && ir_optimize(&ir, &diagnostics);

    int passed = success &&
        ir.count == 2 &&
        strcmp(ir.instructions[1].arg1, "10") == 0 &&
        strcmp(ir.instructions[1].result, "b") == 0;

    printf("%s: Constant propagation\n",
           passed ? "PASS" : "FAIL");

    ast_free(root);
    ir_free(&ir);
    diag_list_free(&diagnostics);

    return passed;
}

static int test_dead_code_elimination(void)
{
    IRProgram ir;
    DiagnosticList diagnostics;

    ir_init(&ir);
    diag_list_init(&diagnostics);

    ASTNode *root = ast_new_program(1, 1);

    ASTNode *left = ast_new_binary(
        "+",
        ast_new_integer(2, 1, 5),
        ast_new_integer(3, 1, 8),
        1, 6
    );

    ASTNode *right = ast_new_binary(
        "+",
        ast_new_integer(4, 1, 12),
        ast_new_integer(5, 1, 15),
        1, 13
    );

    ASTNode *expression = ast_new_binary(
        "+", left, right, 1, 10
    );

    ast_append_statement(root,
        ast_new_declaration("a", "int", expression, 1, 1)
    );

    int success = ir_generate(root, &ir, &diagnostics);
    success = success && ir_optimize(&ir, &diagnostics);

    int passed = success &&
        ir.count == 1 &&
        strcmp(ir.instructions[0].op, "=") == 0 &&
        strcmp(ir.instructions[0].arg1, "14") == 0 &&
        strcmp(ir.instructions[0].result, "a") == 0;

    printf("%s: Dead code elimination\n",
           passed ? "PASS" : "FAIL");

    ast_free(root);
    ir_free(&ir);
    diag_list_free(&diagnostics);

    return passed;
}

int main(void)
{
    int passed = 0;
    int total = 3;

    printf("\n===== OPTIMIZER TESTS =====\n\n");

    passed += test_constant_folding();
    passed += test_constant_propagation();
    passed += test_dead_code_elimination();

    printf("\nResults: %d/%d tests passed\n", passed, total);

    return passed == total ? 0 : 1;

}

