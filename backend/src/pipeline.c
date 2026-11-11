#include "pipeline.h"

#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include "symbol_table.h"

void pipeline_init(PipelineResult *r)
{
    memset(r, 0, sizeof(*r));
    token_array_init(&r->tokens);
    diag_list_init(&r->diags);
    ir_init(&r->ir_raw);
    ir_init(&r->ir_opt);
    vm_program_init(&r->code);
}

void pipeline_free(PipelineResult *r)
{
    if (!r) return;
    ast_free(r->ast);
    token_array_free(&r->tokens);
    diag_list_free(&r->diags);
    ir_free(&r->ir_raw);
    ir_free(&r->ir_opt);
    vm_program_free(&r->code);
    r->ast = NULL;
}

/* ------------------------------------------------------------------ checks
 * Two restrictions that come from how the IR names things:
 *   1. temporaries are called t0, t1, ... so a user variable with such a name
 *      would be overwritten by the compiler;
 *   2. the IR has no scope information, so an inner variable that re-uses the
 *      name of an outer one ("shadowing") would overwrite the outer variable.
 * Both are reported as clear errors instead of producing a wrong program.
 */
static int looks_like_temp(const char *name)
{
    if (name[0] != 't' || name[1] == '\0') return 0;
    for (int i = 1; name[i]; i++)
        if (name[i] < '0' || name[i] > '9') return 0;
    return 1;
}

static int check_node(const ASTNode *node, SymbolTable *table, DiagnosticList *diags)
{
    if (!node) return 1;
    int ok = 1;

    switch (node->type) {
    case AST_DECLARATION: {
        const char *name = node->data.declaration.name;
        if (looks_like_temp(name)) {
            diag_list_add(diags, DIAG_STAGE_CODEGEN, DIAG_ERROR, node->line, node->column,
                          "Variable name '%s' is reserved for compiler temporaries (t0, t1, ...); rename it", name);
            ok = 0;
        } else if (symbol_table_lookup(table, name)) {
            diag_list_add(diags, DIAG_STAGE_CODEGEN, DIAG_ERROR, node->line, node->column,
                          "Variable '%s' hides a variable from an outer block; "
                          "shadowing is not supported by the VM, use a different name", name);
            ok = 0;
        } else {
            symbol_table_insert(table, name, "int");
        }
        ok = check_node(node->data.declaration.initializer, table, diags) && ok;
        break;
    }
    case AST_BLOCK:
        symbol_table_enter_scope(table);
        for (int i = 0; i < node->data.block.count; i++)
            ok = check_node(node->data.block.statements[i], table, diags) && ok;
        symbol_table_exit_scope(table);
        break;
    case AST_PROGRAM:
        for (int i = 0; i < node->data.program.count; i++)
            ok = check_node(node->data.program.statements[i], table, diags) && ok;
        break;
    case AST_IF:
        ok = check_node(node->data.if_statement.then_branch, table, diags) && ok;
        ok = check_node(node->data.if_statement.else_branch, table, diags) && ok;
        break;
    case AST_WHILE:
        ok = check_node(node->data.while_statement.body, table, diags) && ok;
        break;
    default:
        break;
    }
    return ok;
}

static int check_for_vm(const ASTNode *ast, DiagnosticList *diags)
{
    SymbolTable table;
    symbol_table_init(&table);
    int ok = check_node(ast, &table, diags);
    symbol_table_free(&table);
    return ok;
}

/* ------------------------------------------------------------------ compile */

static int fail(PipelineResult *r, DiagStage stage)
{
    r->failed = 1;
    r->failed_stage = stage;
    return 0;
}

int pipeline_compile(const char *source, PipelineResult *r)
{
    if (!source) source = "";

    if (lexer_tokenize(source, &r->tokens, &r->diags) != 0)
        return fail(r, DIAG_STAGE_LEXICAL);

    r->ast = parser_parse(&r->tokens, &r->diags);
    if (!r->ast)
        return fail(r, DIAG_STAGE_SYNTAX);

    if (!semantic_analyze(r->ast, &r->diags))
        return fail(r, DIAG_STAGE_SEMANTIC);

    if (!check_for_vm(r->ast, &r->diags))
        return fail(r, DIAG_STAGE_CODEGEN);

    if (!ir_generate(r->ast, &r->ir_raw, &r->diags) ||
        !ir_generate(r->ast, &r->ir_opt, &r->diags))
        return fail(r, DIAG_STAGE_IR);

    if (!ir_optimize(&r->ir_opt, &r->diags))
        return fail(r, DIAG_STAGE_OPTIMIZER);

    if (!codegen_generate(&r->ir_opt, r->ast, &r->code, &r->diags))
        return fail(r, DIAG_STAGE_CODEGEN);

    r->compiled = 1;
    return 1;
}

/* --------------------------------------------------------------------- JSON */

static void write_ir_text(StrBuf *sb, const IRInstruction *in)
{
    const char *op = in->op;
    const char *a = in->arg1 ? in->arg1 : "";
    const char *b = in->arg2 ? in->arg2 : "";
    const char *res = in->result ? in->result : "";

    if (strcmp(op, "label") == 0)        sb_appendf(sb, "%s:", res);
    else if (strcmp(op, "goto") == 0)    sb_appendf(sb, "goto %s", res);
    else if (strcmp(op, "ifFalse") == 0) sb_appendf(sb, "ifFalse %s goto %s", a, res);
    else if (strcmp(op, "=") == 0)       sb_appendf(sb, "%s = %s", res, a);
    else if (in->arg2)                   sb_appendf(sb, "%s = %s %s %s", res, a, op, b);
    else                                 sb_appendf(sb, "%s = %s%s", res, op, a);
}

static void write_optional_string(StrBuf *sb, const char *s)
{
    if (s) sb_append_json_string(sb, s);
    else sb_append(sb, "null");
}

static void write_ir_json(StrBuf *sb, const IRProgram *ir)
{
    sb_append_char(sb, '[');
    for (int i = 0; i < ir->count; i++) {
        const IRInstruction *in = &ir->instructions[i];
        StrBuf text;
        sb_init(&text);
        write_ir_text(&text, in);

        if (i > 0) sb_append_char(sb, ',');
        sb_appendf(sb, "{\"index\":%d,\"op\":", i);
        write_optional_string(sb, in->op);
        sb_append(sb, ",\"arg1\":");
        write_optional_string(sb, in->arg1);
        sb_append(sb, ",\"arg2\":");
        write_optional_string(sb, in->arg2);
        sb_append(sb, ",\"result\":");
        write_optional_string(sb, in->result);
        sb_appendf(sb, ",\"line\":%d,\"text\":", in->line);
        sb_append_json_string(sb, sb_cstr(&text));
        sb_append_char(sb, '}');
        sb_free(&text);
    }
    sb_append_char(sb, ']');
}

static void write_bytecode_json(StrBuf *sb, const VMProgram *p)
{
    char text[96];
    sb_append_char(sb, '[');
    for (int i = 0; i < p->code_count; i++) {
        const VMInstruction *in = &p->code[i];
        vm_format_instruction(p, i, text, sizeof(text));

        if (i > 0) sb_append_char(sb, ',');
        sb_appendf(sb, "{\"address\":%d,\"opcode\":\"%s\",\"operand\":", i, vm_opcode_name(in->op));
        if (vm_opcode_has_operand(in->op)) sb_appendf(sb, "%d", (int)in->operand);
        else sb_append(sb, "null");
        sb_appendf(sb, ",\"line\":%d,\"text\":", in->line);
        sb_append_json_string(sb, text);
        sb_append_char(sb, '}');
    }
    sb_append_char(sb, ']');
}

static void write_int_list(StrBuf *sb, const int32_t *values, int count)
{
    sb_append_char(sb, '[');
    for (int i = 0; i < count; i++) {
        if (i > 0) sb_append_char(sb, ',');
        sb_appendf(sb, "%d", (int)values[i]);
    }
    sb_append_char(sb, ']');
}

static void write_user_memory(StrBuf *sb, const VM *vm, const int *user_slots, int user_count)
{
    sb_append_char(sb, '[');
    for (int i = 0; i < user_count; i++) {
        if (i > 0) sb_append_char(sb, ',');
        sb_appendf(sb, "%d", (int)vm->memory[user_slots[i]]);
    }
    sb_append_char(sb, ']');
}

static void write_trace_entry(StrBuf *sb, const VM *vm, int step, int first,
                              const int *user_slots, int user_count)
{
    char text[96];
    if (!first) sb_append_char(sb, ',');

    sb_appendf(sb, "{\"step\":%d,", step);
    if (step == 0) {
        sb_append(sb, "\"pc\":-1,\"nextPc\":0,\"instruction\":\"(start)\",\"line\":0,");
    } else {
        vm_format_instruction(vm->program, vm->last_pc, text, sizeof(text));
        sb_appendf(sb, "\"pc\":%d,\"nextPc\":%d,\"instruction\":", vm->last_pc, vm->pc);
        sb_append_json_string(sb, text);
        sb_appendf(sb, ",\"line\":%d,", vm->last_line);
    }
    sb_append(sb, "\"stack\":");
    write_int_list(sb, vm->stack, vm->sp);
    sb_append(sb, ",\"memory\":");
    write_user_memory(sb, vm, user_slots, user_count);
    if (vm->status == VM_ERROR) {
        sb_append(sb, ",\"error\":");
        sb_append_json_string(sb, vm->error);
    }
    sb_append_char(sb, '}');
}

/* Runs the VM, writes the "execution" object. Adds a runtime diagnostic on error.
 * Returns 0 only on out-of-memory. */
static int execute(PipelineResult *r, long max_steps, StrBuf *out)
{
    const VMProgram *p = &r->code;
    VM vm;
    if (!vm_init(&vm, p)) return 0;

    int *user_slots = malloc((size_t)(p->var_count > 0 ? p->var_count : 1) * sizeof(int));
    if (!user_slots) { vm_free(&vm); return 0; }

    int user_count = 0;
    for (int i = 0; i < p->var_count; i++)
        if (!p->vars[i].is_temp) user_slots[user_count++] = i;

    StrBuf trace;
    sb_init(&trace);
    sb_append_char(&trace, '[');
    write_trace_entry(&trace, &vm, 0, 1, user_slots, user_count);

    int recorded = 1, truncated = 0;
    while (vm.status == VM_RUNNING) {
        if (vm.steps >= max_steps) {
            snprintf(vm.error, sizeof(vm.error), "Step limit exceeded (possible infinite loop)");
            vm.status = VM_ERROR;
            break;
        }
        vm_step(&vm);
        if (recorded < PIPELINE_TRACE_MAX) {
            write_trace_entry(&trace, &vm, (int)vm.steps, 0, user_slots, user_count);
            recorded++;
        } else {
            truncated = 1;
        }
    }
    sb_append_char(&trace, ']');

    if (vm.status == VM_ERROR)
        diag_list_add(&r->diags, DIAG_STAGE_RUNTIME, DIAG_ERROR, vm.last_line, 0, "%s", vm.error);

    sb_appendf(out, "{\"status\":\"%s\",\"steps\":%ld,\"error\":",
               vm.status == VM_HALTED ? "halted" : "error", vm.steps);
    if (vm.status == VM_ERROR) sb_append_json_string(out, vm.error);
    else sb_append(out, "null");
    sb_appendf(out, ",\"traceTruncated\":%s,\"variables\":[", truncated ? "true" : "false");
    for (int i = 0; i < user_count; i++) {
        if (i > 0) sb_append_char(out, ',');
        sb_append_json_string(out, p->vars[user_slots[i]].name);
    }
    sb_append(out, "],\"finalMemory\":");
    write_user_memory(out, &vm, user_slots, user_count);
    sb_append(out, ",\"trace\":");
    sb_append(out, sb_cstr(&trace));
    sb_append_char(out, '}');

    int ok = !trace.failed;
    sb_free(&trace);
    free(user_slots);
    vm_free(&vm);
    return ok;
}

char *pipeline_to_json(PipelineResult *r, long max_steps)
{
    StrBuf execution;
    sb_init(&execution);
    int have_execution = 0;

    if (r->compiled) {
        if (!execute(r, max_steps, &execution)) { sb_free(&execution); return NULL; }
        have_execution = 1;
    }

    const int runtime_failed = diag_list_error_count(&r->diags) > 0 && r->compiled;
    const int success = r->compiled && !runtime_failed;
    const char *stage = r->failed ? diag_stage_name(r->failed_stage)
                                  : (runtime_failed ? "runtime" : "ok");

    StrBuf sb;
    sb_init(&sb);
    sb_appendf(&sb, "{\"success\":%s,\"stage\":\"%s\",\"tokens\":", success ? "true" : "false", stage);
    token_array_write_json(&r->tokens, &sb);
    sb_append(&sb, ",\"ast\":");
    if (r->ast) ast_write_json(r->ast, &sb); else sb_append(&sb, "null");
    sb_append(&sb, ",\"diagnostics\":");
    diag_list_write_json(&r->diags, &sb);
    sb_append(&sb, ",\"ir\":");
    write_ir_json(&sb, &r->ir_raw);
    sb_append(&sb, ",\"optimizedIr\":");
    write_ir_json(&sb, &r->ir_opt);
    sb_append(&sb, ",\"bytecode\":");
    write_bytecode_json(&sb, &r->code);
    sb_append(&sb, ",\"execution\":");
    sb_append(&sb, have_execution ? sb_cstr(&execution) : "null");
    sb_append_char(&sb, '}');

    sb_free(&execution);
    return sb_detach(&sb);
}

char *pipeline_run_json(const char *source, long max_steps)
{
    PipelineResult r;
    pipeline_init(&r);
    pipeline_compile(source, &r);
    char *json = pipeline_to_json(&r, max_steps);
    pipeline_free(&r);
    return json;
}