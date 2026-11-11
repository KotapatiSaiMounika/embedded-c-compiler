/* pipeline_tests.c - end-to-end: source text -> ... -> VM result. */
#include "test_util.h"
#include "pipeline.h"

#include <stdlib.h>

/* Compiles + runs `source`; stores the value of variable `name` in *value.
 * Returns 1 if the program compiled and halted normally. */
static int run_get(const char *source, const char *name, int *value)
{
    PipelineResult r;
    pipeline_init(&r);
    int ok = pipeline_compile(source, &r);
    if (ok) {
        VM vm;
        vm_init(&vm, &r.code);
        ok = (vm_run(&vm, VM_DEFAULT_MAX_STEPS) == VM_HALTED);
        for (int i = 0; i < r.code.var_count; i++)
            if (strcmp(r.code.vars[i].name, name) == 0) *value = vm.memory[i];
        vm_free(&vm);
    }
    pipeline_free(&r);
    return ok;
}

#define EXPECT_VALUE(src, var, expected)                    \
    do {                                                    \
        int v_ = -12345;                                    \
        CHECK(run_get((src), (var), &v_));                  \
        CHECK_INT(v_, (expected));                          \
    } while (0)

static void test_project_example(void)
{
    EXPECT_VALUE("int x=2; while(x<5){x=x+1;}", "x", 5);
}

static void test_expressions(void)
{
    EXPECT_VALUE("int a = 2 + 3 * 4;", "a", 14);
    EXPECT_VALUE("int a = (2 + 3) * 4;", "a", 20);
    EXPECT_VALUE("int a = 20 - 5 - 3;", "a", 12);     /* left associative */
    EXPECT_VALUE("int a = 100 / 5 / 2;", "a", 10);
    EXPECT_VALUE("int a = -7 / 2;", "a", -3);
    EXPECT_VALUE("int a = -(3 + 4);", "a", -7);
    EXPECT_VALUE("int a = !0 + !5;", "a", 1);
    EXPECT_VALUE("int a = 1 < 2 && 2 < 3;", "a", 1);
    EXPECT_VALUE("int a = 0 || 0;", "a", 0);
    EXPECT_VALUE("int a = 3 == 3;", "a", 1);
    EXPECT_VALUE("int a = 3 != 3;", "a", 0);
}

static void test_variables(void)
{
    EXPECT_VALUE("int a = 4; int b = a * a + 1;", "b", 17);
    EXPECT_VALUE("int a = 1, b = 2, c = a + b;", "c", 3);
    EXPECT_VALUE("int a; int b = a + 5;", "b", 5);       /* uninitialised = 0 */
    EXPECT_VALUE("int a = 1; a = a + 1; a = a * 10;", "a", 20);
}

static void test_if_else(void)
{
    EXPECT_VALUE("int x = 5; int y = 0; if (x > 3) { y = 1; } else { y = 2; }", "y", 1);
    EXPECT_VALUE("int x = 1; int y = 0; if (x > 3) { y = 1; } else { y = 2; }", "y", 2);
    EXPECT_VALUE("int x = 1; int y = 7; if (x > 3) { y = 1; }", "y", 7);
    EXPECT_VALUE("int x = 2; int y = 0; if (x == 1) y = 10; else if (x == 2) y = 20; else y = 30;", "y", 20);
}

static void test_loops(void)
{
    EXPECT_VALUE("int i = 0; int s = 0; while (i < 10) { i = i + 1; s = s + i; }", "s", 55);
    EXPECT_VALUE("int n = 5; int f = 1; while (n > 1) { f = f * n; n = n - 1; }", "f", 120);
    EXPECT_VALUE("int i = 0; int c = 0; while (i < 3) { int j = 0; while (j < 4) { c = c + 1; j = j + 1; } i = i + 1; }", "c", 12);
    EXPECT_VALUE("int x = 0; while (0) { x = 1; }", "x", 0);
}

static void test_optimizer_does_not_change_result(void)
{
    EXPECT_VALUE("int a = 10; int b = 20; int c = a + b; int d = c * 2;", "d", 60);
    EXPECT_VALUE("int a = 1; if (a) { a = 2; } int b = a + 1;", "b", 3);
}

static void test_wraparound(void)
{
    EXPECT_VALUE("int a = 2147483647; int b = a + 1;", "b", -2147483647 - 1);
}

static void test_runtime_division_by_zero(void)
{
    int v = 0;
    CHECK(!run_get("int z = 0; int a = 10 / z;", "a", &v));

    char *json = pipeline_run_json("int z = 0; int a = 10 / z;", VM_DEFAULT_MAX_STEPS);
    CHECK(json != NULL);
    if (json) {
        CHECK_CONTAINS(json, "\"success\":false");
        CHECK_CONTAINS(json, "\"stage\":\"runtime\"");
        CHECK_CONTAINS(json, "Division by zero");
    }
    free(json);
}

static void test_infinite_loop_is_stopped(void)
{
    char *json = pipeline_run_json("int x = 1; while (x) { x = 1; }", 5000);
    CHECK(json != NULL);
    if (json) {
        CHECK_CONTAINS(json, "Step limit exceeded");
        CHECK_CONTAINS(json, "\"traceTruncated\":true");
    }
    free(json);
}

static void expect_failure(const char *src, const char *stage, const char *message)
{
    PipelineResult r;
    pipeline_init(&r);
    int ok = pipeline_compile(src, &r);
    CHECK_INT(ok, 0);
    CHECK_INT(r.failed, 1);
    CHECK_STR(diag_stage_name(r.failed_stage), stage);
    CHECK(r.diags.count > 0);
    if (r.diags.count > 0) CHECK_CONTAINS(r.diags.items[0].message, message);
    pipeline_free(&r);
}

static void test_compile_errors(void)
{
    expect_failure("int a = 5 @ 2;", "lexical", "");
    expect_failure("int a = ;", "syntax", "");
    expect_failure("int a = b + 1;", "semantic", "not declared");
    expect_failure("int a = 1; int a = 2;", "semantic", "already declared");
    expect_failure("int t0 = 1;", "codegen", "reserved");
    expect_failure("int x = 1; { int x = 2; }", "codegen", "hides");
}

static void test_json_shape(void)
{
    char *json = pipeline_run_json("int x = 2; while(x<5){x=x+1;}", VM_DEFAULT_MAX_STEPS);
    CHECK(json != NULL);
    if (!json) return;
    CHECK_CONTAINS(json, "\"success\":true");
    CHECK_CONTAINS(json, "\"stage\":\"ok\"");
    CHECK_CONTAINS(json, "\"tokens\":[");
    CHECK_CONTAINS(json, "\"ast\":{");
    CHECK_CONTAINS(json, "\"ir\":[");
    CHECK_CONTAINS(json, "\"optimizedIr\":[");
    CHECK_CONTAINS(json, "\"bytecode\":[");
    CHECK_CONTAINS(json, "\"status\":\"halted\"");
    CHECK_CONTAINS(json, "\"variables\":[\"x\"]");
    CHECK_CONTAINS(json, "\"finalMemory\":[5]");
    free(json);

    json = pipeline_run_json("int a = ;", VM_DEFAULT_MAX_STEPS);
    CHECK(json != NULL);
    if (json) {
        CHECK_CONTAINS(json, "\"success\":false");
        CHECK_CONTAINS(json, "\"stage\":\"syntax\"");
        CHECK_CONTAINS(json, "\"execution\":null");
    }
    free(json);
}

static void test_empty_source(void)
{
    char *json = pipeline_run_json("", VM_DEFAULT_MAX_STEPS);
    CHECK(json != NULL);
    if (json) CHECK_CONTAINS(json, "\"stage\":");
    free(json);
}

int main(void)
{
    RUN_TEST(test_project_example);
    RUN_TEST(test_expressions);
    RUN_TEST(test_variables);
    RUN_TEST(test_if_else);
    RUN_TEST(test_loops);
    RUN_TEST(test_optimizer_does_not_change_result);
    RUN_TEST(test_wraparound);
    RUN_TEST(test_runtime_division_by_zero);
    RUN_TEST(test_infinite_loop_is_stopped);
    RUN_TEST(test_compile_errors);
    RUN_TEST(test_json_shape);
    RUN_TEST(test_empty_source);
    return TEST_SUMMARY();
}