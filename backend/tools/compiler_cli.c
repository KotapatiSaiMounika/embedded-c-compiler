/*
 * compiler_cli - compile and run a program from the command line (no server).
 *
 *   ./compiler_cli program.c              final variable values
 *   ./compiler_cli --ir program.c         + IR before/after optimization
 *   ./compiler_cli --bytecode program.c   + bytecode listing
 *   ./compiler_cli --trace program.c      + step-by-step execution
 *   ./compiler_cli --json program.c       the exact JSON the REST API returns
 *   echo "int x = 5;" | ./compiler_cli -  (read from stdin)
 *
 * Exit code: 0 = compiled and ran, 1 = compile or runtime error, 2 = usage error.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pipeline.h"

static char *read_all(FILE *f)
{
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += n;
        if (len + 1 >= cap) {
            cap *= 2;
            char *bigger = realloc(buf, cap);
            if (!bigger) { free(buf); return NULL; }
            buf = bigger;
        }
    }
    buf[len] = '\0';
    return buf;
}

static void print_ir(const char *title, const IRProgram *ir)
{
    printf("\n=== %s (%d instructions) ===\n", title, ir->count);
    for (int i = 0; i < ir->count; i++) {
        const IRInstruction *in = &ir->instructions[i];
        printf("%3d  %s", i, in->op);
        if (in->arg1) printf(" %s", in->arg1);
        if (in->arg2) printf(", %s", in->arg2);
        if (in->result) printf(" -> %s", in->result);
        printf("\n");
    }
}

static void print_bytecode(const VMProgram *p)
{
    char text[96];
    printf("\n=== BYTECODE (%d instructions, %d memory slots) ===\n", p->code_count, p->var_count);
    for (int i = 0; i < p->code_count; i++) {
        vm_format_instruction(p, i, text, sizeof(text));
        printf("%4d  %-14s (line %d)\n", i, text, p->code[i].line);
    }
}

static void print_diagnostics(const DiagnosticList *d)
{
    if (d->count == 0) return;
    printf("\n=== DIAGNOSTICS ===\n");
    for (int i = 0; i < d->count; i++) {
        const Diagnostic *x = &d->items[i];
        printf("[%s] %s at line %d, col %d: %s\n", diag_stage_name(x->stage),
               diag_severity_name(x->severity), x->line, x->column, x->message);
    }
}

static int run_and_print(PipelineResult *r, int trace)
{
    char text[96];
    VM vm;
    if (!vm_init(&vm, &r->code)) { fprintf(stderr, "out of memory\n"); return 1; }

    if (trace) printf("\n=== EXECUTION TRACE ===\n");
    while (vm.status == VM_RUNNING) {
        if (vm.steps >= VM_DEFAULT_MAX_STEPS) {
            snprintf(vm.error, sizeof(vm.error), "Step limit exceeded (possible infinite loop)");
            vm.status = VM_ERROR;
            break;
        }
        vm_step(&vm);
        if (trace) {
            vm_format_instruction(&r->code, vm.last_pc, text, sizeof(text));
            printf("step %-4ld pc=%-3d %-14s stack=[", vm.steps, vm.last_pc, text);
            for (int i = 0; i < vm.sp; i++) printf("%s%d", i ? " " : "", (int)vm.stack[i]);
            printf("]\n");
        }
    }

    int ok = (vm.status == VM_HALTED);
    printf("\n=== RESULT (%ld steps) ===\n", vm.steps);
    if (!ok) printf("RUNTIME ERROR at line %d: %s\n", vm.last_line, vm.error);
    for (int i = 0; i < r->code.var_count; i++)
        if (!r->code.vars[i].is_temp)
            printf("%s = %d\n", r->code.vars[i].name, (int)vm.memory[i]);

    vm_free(&vm);
    return ok;
}

int main(int argc, char **argv)
{
    int json = 0, show_ir = 0, show_bc = 0, trace = 0;
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) json = 1;
        else if (strcmp(argv[i], "--ir") == 0) show_ir = 1;
        else if (strcmp(argv[i], "--bytecode") == 0) show_bc = 1;
        else if (strcmp(argv[i], "--trace") == 0) trace = 1;
        else path = argv[i];
    }
    if (!path) {
        fprintf(stderr, "usage: %s [--json] [--ir] [--bytecode] [--trace] <file.c | ->\n", argv[0]);
        return 2;
    }

    FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
    if (!f) { perror(path); return 2; }
    char *source = read_all(f);
    if (f != stdin) fclose(f);
    if (!source) { fprintf(stderr, "out of memory\n"); return 2; }

    int exit_code = 0;

    if (json) {
        char *out = pipeline_run_json(source, VM_DEFAULT_MAX_STEPS);
        if (out) { puts(out); exit_code = strstr(out, "\"success\":true") ? 0 : 1; free(out); }
        else { fprintf(stderr, "out of memory\n"); exit_code = 2; }
        free(source);
        return exit_code;
    }

    PipelineResult r;
    pipeline_init(&r);
    int compiled = pipeline_compile(source, &r);

    if (show_ir && r.ir_raw.count > 0) {
        print_ir("IR (before optimization)", &r.ir_raw);
        print_ir("IR (after optimization)", &r.ir_opt);
    }
    if (show_bc && compiled) print_bytecode(&r.code);

    if (compiled) exit_code = run_and_print(&r, trace) ? 0 : 1;
    else {
        printf("COMPILATION FAILED in %s stage\n", diag_stage_name(r.failed_stage));
        exit_code = 1;
    }
    print_diagnostics(&r.diags);

    pipeline_free(&r);
    free(source);
    return exit_code;
}