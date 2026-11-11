/* codegen_tests.c - IR -> bytecode translation (IR is built by hand, no parser involved). */
#include "test_util.h"
#include "codegen.h"
#include "vm.h"

#include <stdlib.h>

static void add(IRProgram *ir, const char *op, const char *a1, const char *a2, const char *res)
{
    /* ir_generate normally fills the program; here we build instructions manually */
    if (ir->count == ir->capacity) {
        ir->capacity = ir->capacity ? ir->capacity * 2 : 8;
        ir->instructions = realloc(ir->instructions, (size_t)ir->capacity * sizeof(IRInstruction));
    }
    IRInstruction *in = &ir->instructions[ir->count++];
    in->op = str_dup(op);
    in->arg1 = a1 ? str_dup(a1) : NULL;
    in->arg2 = a2 ? str_dup(a2) : NULL;
    in->result = res ? str_dup(res) : NULL;
    in->line = 1;
}

static void check_listing(const VMProgram *p, const char *const *expected, int n)
{
    CHECK_INT(p->code_count, n);
    char buf[64];
    for (int i = 0; i < n && i < p->code_count; i++) {
        vm_format_instruction(p, i, buf, sizeof buf);
        CHECK_STR(buf, expected[i]);
    }
}

static void test_assignment_and_binary(void)
{
    IRProgram ir; ir_init(&ir);
    DiagnosticList d; diag_list_init(&d);
    add(&ir, "+", "a", "5", "t0");
    add(&ir, "=", "t0", NULL, "b");

    VMProgram p; vm_program_init(&p);
    CHECK_INT(codegen_generate(&ir, NULL, &p, &d), 1);
    const char *expected[] = { "LOAD a", "PUSH 5", "ADD", "STORE t0", "LOAD t0", "STORE b", "HALT" };
    check_listing(&p, expected, 7);
    CHECK_INT(p.var_count, 3); /* a, t0, b */

    vm_program_free(&p); ir_free(&ir); diag_list_free(&d);
}

static void test_unary_and_negative_constant(void)
{
    IRProgram ir; ir_init(&ir);
    DiagnosticList d; diag_list_init(&d);
    add(&ir, "=", "-5", NULL, "x");
    add(&ir, "!", "x", NULL, "t0");
    add(&ir, "-", "x", NULL, "t1");

    VMProgram p; vm_program_init(&p);
    CHECK_INT(codegen_generate(&ir, NULL, &p, &d), 1);
    const char *expected[] = { "PUSH -5", "STORE x", "LOAD x", "NOT", "STORE t0",
                               "LOAD x", "NEG", "STORE t1", "HALT" };
    check_listing(&p, expected, 9);

    vm_program_free(&p); ir_free(&ir); diag_list_free(&d);
}

static void test_labels_and_jumps(void)
{
    /* L0: ifFalse c -> L1 ; goto L0 ; L1: */
    IRProgram ir; ir_init(&ir);
    DiagnosticList d; diag_list_init(&d);
    add(&ir, "label", NULL, NULL, "L0");
    add(&ir, "ifFalse", "c", NULL, "L1");
    add(&ir, "goto", NULL, NULL, "L0");
    add(&ir, "label", NULL, NULL, "L1");

    VMProgram p; vm_program_init(&p);
    CHECK_INT(codegen_generate(&ir, NULL, &p, &d), 1);
    const char *expected[] = { "LOAD c", "JZ 3", "JMP 0", "HALT" };
    check_listing(&p, expected, 4);

    vm_program_free(&p); ir_free(&ir); diag_list_free(&d);
}

static void test_undefined_label_is_error(void)
{
    IRProgram ir; ir_init(&ir);
    DiagnosticList d; diag_list_init(&d);
    add(&ir, "goto", NULL, NULL, "L9");

    VMProgram p; vm_program_init(&p);
    CHECK_INT(codegen_generate(&ir, NULL, &p, &d), 0);
    CHECK_INT(diag_list_error_count(&d), 1);
    CHECK_INT(d.items[0].stage, DIAG_STAGE_CODEGEN);
    CHECK_CONTAINS(d.items[0].message, "undefined label");

    vm_program_free(&p); ir_free(&ir); diag_list_free(&d);
}

static void test_unknown_operation_is_error(void)
{
    IRProgram ir; ir_init(&ir);
    DiagnosticList d; diag_list_init(&d);
    add(&ir, "%", "a", "b", "t0");

    VMProgram p; vm_program_init(&p);
    CHECK_INT(codegen_generate(&ir, NULL, &p, &d), 0);
    CHECK_CONTAINS(d.items[0].message, "Unknown IR operation");

    vm_program_free(&p); ir_free(&ir); diag_list_free(&d);
}

static void test_empty_program_is_just_halt(void)
{
    IRProgram ir; ir_init(&ir);
    DiagnosticList d; diag_list_init(&d);
    VMProgram p; vm_program_init(&p);
    CHECK_INT(codegen_generate(&ir, NULL, &p, &d), 1);
    CHECK_INT(p.code_count, 1);
    CHECK_INT(p.code[0].op, OP_HALT);
    vm_program_free(&p); ir_free(&ir); diag_list_free(&d);
}

int main(void)
{
    RUN_TEST(test_assignment_and_binary);
    RUN_TEST(test_unary_and_negative_constant);
    RUN_TEST(test_labels_and_jumps);
    RUN_TEST(test_undefined_label_is_error);
    RUN_TEST(test_unknown_operation_is_error);
    RUN_TEST(test_empty_program_is_just_halt);
    return TEST_SUMMARY();
}