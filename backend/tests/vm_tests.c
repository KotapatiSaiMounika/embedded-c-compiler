/* vm_tests.c - tests the virtual machine with hand-written bytecode (no compiler involved). */
#include "test_util.h"
#include "vm.h"

#include <limits.h>

/* Runs `p` to the end and returns the final status. */
static VMStatus run(const VMProgram *p, VM *vm)
{
    vm_init(vm, p);
    return vm_run(vm, 1000);
}

static void test_push_store_load(void)
{
    VMProgram p; vm_program_init(&p);
    int x = vm_program_slot(&p, "x");
    vm_program_emit(&p, OP_PUSH, 7, 1);
    vm_program_emit(&p, OP_STORE, x, 1);
    vm_program_emit(&p, OP_LOAD, x, 1);
    vm_program_emit(&p, OP_HALT, 0, 0);

    VM vm;
    CHECK_INT(run(&p, &vm), VM_HALTED);
    CHECK_INT(vm.memory[x], 7);
    CHECK_INT(vm.sp, 1);
    CHECK_INT(vm.stack[0], 7);
    CHECK_INT(vm.steps, 4);
    vm_free(&vm); vm_program_free(&p);
}

/* a OP b leaves the result on the stack */
static int binary(OpCode op, int32_t a, int32_t b, VMStatus *status, char *err)
{
    VMProgram p; vm_program_init(&p);
    vm_program_emit(&p, OP_PUSH, a, 1);
    vm_program_emit(&p, OP_PUSH, b, 1);
    vm_program_emit(&p, op, 0, 1);
    vm_program_emit(&p, OP_HALT, 0, 0);
    VM vm;
    *status = run(&p, &vm);
    int result = vm.sp > 0 ? vm.stack[vm.sp - 1] : -999;
    if (err) snprintf(err, 128, "%s", vm.error);
    vm_free(&vm); vm_program_free(&p);
    return result;
}

static void test_arithmetic(void)
{
    VMStatus s;
    CHECK_INT(binary(OP_ADD, 2, 3, &s, NULL), 5);
    CHECK_INT(binary(OP_SUB, 2, 3, &s, NULL), -1);
    CHECK_INT(binary(OP_MUL, 4, -3, &s, NULL), -12);
    CHECK_INT(binary(OP_DIV, 7, 2, &s, NULL), 3);
    CHECK_INT(binary(OP_DIV, -7, 2, &s, NULL), -3); /* truncates toward zero, like C */
}

static void test_comparisons_and_logic(void)
{
    VMStatus s;
    CHECK_INT(binary(OP_LT, 1, 2, &s, NULL), 1);
    CHECK_INT(binary(OP_LT, 2, 2, &s, NULL), 0);
    CHECK_INT(binary(OP_GT, 3, 2, &s, NULL), 1);
    CHECK_INT(binary(OP_LE, 2, 2, &s, NULL), 1);
    CHECK_INT(binary(OP_GE, 1, 2, &s, NULL), 0);
    CHECK_INT(binary(OP_EQ, 4, 4, &s, NULL), 1);
    CHECK_INT(binary(OP_NE, 4, 4, &s, NULL), 0);
    CHECK_INT(binary(OP_AND, 5, 0, &s, NULL), 0);
    CHECK_INT(binary(OP_AND, 5, 9, &s, NULL), 1);
    CHECK_INT(binary(OP_OR, 0, 0, &s, NULL), 0);
    CHECK_INT(binary(OP_OR, 0, 7, &s, NULL), 1);
}

static void test_overflow_wraps(void)
{
    VMStatus s;
    CHECK_INT(binary(OP_ADD, INT_MAX, 1, &s, NULL), INT_MIN);
    CHECK_INT(s, VM_HALTED);
    CHECK_INT(binary(OP_MUL, 65536, 65536, &s, NULL), 0);
}

static void test_division_errors(void)
{
    VMStatus s; char err[128];
    binary(OP_DIV, 1, 0, &s, err);
    CHECK_INT(s, VM_ERROR);
    CHECK_STR(err, "Division by zero");

    binary(OP_DIV, INT_MIN, -1, &s, err);
    CHECK_INT(s, VM_ERROR);
    CHECK_CONTAINS(err, "overflow");
}

static void test_unary(void)
{
    VMProgram p; vm_program_init(&p);
    vm_program_emit(&p, OP_PUSH, 5, 1);
    vm_program_emit(&p, OP_NEG, 0, 1);
    vm_program_emit(&p, OP_PUSH, 0, 1);
    vm_program_emit(&p, OP_NOT, 0, 1);
    vm_program_emit(&p, OP_HALT, 0, 0);
    VM vm;
    CHECK_INT(run(&p, &vm), VM_HALTED);
    CHECK_INT(vm.stack[0], -5);
    CHECK_INT(vm.stack[1], 1);
    vm_free(&vm); vm_program_free(&p);
}

static void test_jumps(void)
{
    /* x = 0; JZ taken skips "x = 1" */
    VMProgram p; vm_program_init(&p);
    int x = vm_program_slot(&p, "x");
    vm_program_emit(&p, OP_PUSH, 0, 1);   /* 0 */
    vm_program_emit(&p, OP_JZ, 4, 1);     /* 1: pops 0 -> jump to 4 */
    vm_program_emit(&p, OP_PUSH, 1, 1);   /* 2 */
    vm_program_emit(&p, OP_STORE, x, 1);  /* 3 */
    vm_program_emit(&p, OP_JMP, 5, 1);    /* 4 */
    vm_program_emit(&p, OP_HALT, 0, 0);   /* 5 */
    VM vm;
    CHECK_INT(run(&p, &vm), VM_HALTED);
    CHECK_INT(vm.memory[x], 0);
    CHECK_INT(vm.steps, 4);
    vm_free(&vm); vm_program_free(&p);
}

static void test_stack_errors(void)
{
    VMProgram p; vm_program_init(&p);
    vm_program_emit(&p, OP_ADD, 0, 1);
    vm_program_emit(&p, OP_HALT, 0, 0);
    VM vm;
    CHECK_INT(run(&p, &vm), VM_ERROR);
    CHECK_STR(vm.error, "Stack underflow");
    vm_free(&vm); vm_program_free(&p);

    /* endless pushing -> overflow */
    vm_program_init(&p);
    vm_program_emit(&p, OP_PUSH, 1, 1);
    vm_program_emit(&p, OP_JMP, 0, 1);
    vm_init(&vm, &p);
    CHECK_INT(vm_run(&vm, 100000), VM_ERROR);
    CHECK_STR(vm.error, "Stack overflow");
    vm_free(&vm); vm_program_free(&p);
}

static void test_step_limit_and_missing_halt(void)
{
    VMProgram p; vm_program_init(&p);
    vm_program_emit(&p, OP_JMP, 0, 1);
    VM vm; vm_init(&vm, &p);
    CHECK_INT(vm_run(&vm, 50), VM_ERROR);
    CHECK_CONTAINS(vm.error, "Step limit");
    CHECK_INT(vm.steps, 50);
    vm_free(&vm); vm_program_free(&p);

    vm_program_init(&p);
    vm_program_emit(&p, OP_PUSH, 1, 1); /* no HALT */
    CHECK_INT(run(&p, &vm), VM_ERROR);
    CHECK_CONTAINS(vm.error, "Program counter");
    vm_free(&vm); vm_program_free(&p);
}

static void test_disassembler(void)
{
    VMProgram p; vm_program_init(&p);
    int total = vm_program_slot(&p, "total");
    vm_program_emit(&p, OP_PUSH, 2, 1);
    vm_program_emit(&p, OP_STORE, total, 1);
    vm_program_emit(&p, OP_JZ, 9, 1);
    vm_program_emit(&p, OP_ADD, 0, 1);
    char buf[64];
    vm_format_instruction(&p, 0, buf, sizeof buf); CHECK_STR(buf, "PUSH 2");
    vm_format_instruction(&p, 1, buf, sizeof buf); CHECK_STR(buf, "STORE total");
    vm_format_instruction(&p, 2, buf, sizeof buf); CHECK_STR(buf, "JZ 9");
    vm_format_instruction(&p, 3, buf, sizeof buf); CHECK_STR(buf, "ADD");
    vm_program_free(&p);
}

static void test_temp_detection(void)
{
    VMProgram p; vm_program_init(&p);
    int t = vm_program_slot(&p, "t12");
    int v = vm_program_slot(&p, "total");
    int w = vm_program_slot(&p, "t");
    CHECK_INT(p.vars[t].is_temp, 1);
    CHECK_INT(p.vars[v].is_temp, 0);
    CHECK_INT(p.vars[w].is_temp, 0);
    CHECK_INT(vm_program_slot(&p, "t12"), t); /* same slot returned */
    vm_program_free(&p);
}

int main(void)
{
    RUN_TEST(test_push_store_load);
    RUN_TEST(test_arithmetic);
    RUN_TEST(test_comparisons_and_logic);
    RUN_TEST(test_overflow_wraps);
    RUN_TEST(test_division_errors);
    RUN_TEST(test_unary);
    RUN_TEST(test_jumps);
    RUN_TEST(test_stack_errors);
    RUN_TEST(test_step_limit_and_missing_halt);
    RUN_TEST(test_disassembler);
    RUN_TEST(test_temp_detection);
    return TEST_SUMMARY();
}