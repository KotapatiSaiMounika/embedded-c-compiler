#include "codegen.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    int address;
} LabelDef;

typedef struct {
    int code_index;       /* instruction whose operand must be patched */
    const char *label;    /* points into the IR (not owned)            */
    int line;
} Fixup;

typedef struct {
    VMProgram *prog;
    DiagnosticList *diags;
    LabelDef *labels; int label_count, label_cap;
    Fixup *fixups;    int fixup_count, fixup_cap;
    int failed;
} Ctx;

static void error(Ctx *c, int line, const char *fmt, const char *arg)
{
    diag_list_add(c->diags, DIAG_STAGE_CODEGEN, DIAG_ERROR, line, 0, fmt, arg ? arg : "");
    c->failed = 1;
}

static int emit(Ctx *c, OpCode op, int32_t operand, int line)
{
    int at = vm_program_emit(c->prog, op, operand, line);
    if (at < 0) error(c, line, "Out of memory during code generation%s", "");
    return at;
}

/* "12" or "-5" (digits only, fits in int32) -> 1 */
static int parse_constant(const char *s, int32_t *value)
{
    if (!s || !*s) return 0;
    const char *p = s;
    if (*p == '-') p++;
    if (!*p) return 0;
    for (const char *q = p; *q; q++)
        if (*q < '0' || *q > '9') return 0;

    char *end;
    long long v = strtoll(s, &end, 10);
    if (*end != '\0' || v < INT32_MIN || v > INT32_MAX) return 0;
    *value = (int32_t)v;
    return 1;
}

/* Pushes the value of an IR operand (constant or variable) on the VM stack. */
static void push_operand(Ctx *c, const char *operand, int line)
{
    if (!operand) { error(c, line, "Missing operand in IR instruction%s", ""); return; }

    int32_t value;
    if (parse_constant(operand, &value)) {
        emit(c, OP_PUSH, value, line);
    } else {
        int slot = vm_program_slot(c->prog, operand);
        if (slot < 0) { error(c, line, "Out of memory during code generation%s", ""); return; }
        emit(c, OP_LOAD, slot, line);
    }
}

static void store_result(Ctx *c, const char *result, int line)
{
    if (!result) { error(c, line, "Missing result in IR instruction%s", ""); return; }
    int slot = vm_program_slot(c->prog, result);
    if (slot < 0) { error(c, line, "Out of memory during code generation%s", ""); return; }
    emit(c, OP_STORE, slot, line);
}

static int binary_opcode(const char *op, OpCode *out)
{
    static const struct { const char *text; OpCode code; } table[] = {
        {"+", OP_ADD}, {"-", OP_SUB}, {"*", OP_MUL}, {"/", OP_DIV},
        {"<", OP_LT},  {">", OP_GT},  {"<=", OP_LE}, {">=", OP_GE},
        {"==", OP_EQ}, {"!=", OP_NE}, {"&&", OP_AND}, {"||", OP_OR}
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        if (strcmp(op, table[i].text) == 0) { *out = table[i].code; return 1; }
    return 0;
}

static void define_label(Ctx *c, const char *name, int line)
{
    for (int i = 0; i < c->label_count; i++)
        if (strcmp(c->labels[i].name, name) == 0) { error(c, line, "Label '%s' defined twice", name); return; }

    if (c->label_count == c->label_cap) {
        int cap = c->label_cap ? c->label_cap * 2 : 16;
        LabelDef *items = realloc(c->labels, (size_t)cap * sizeof(LabelDef));
        if (!items) { error(c, line, "Out of memory during code generation%s", ""); return; }
        c->labels = items; c->label_cap = cap;
    }
    size_t len = strlen(name) + 1;
    char *copy = malloc(len);
    if (!copy) { error(c, line, "Out of memory during code generation%s", ""); return; }
    memcpy(copy, name, len);
    c->labels[c->label_count].name = copy;
    c->labels[c->label_count].address = c->prog->code_count;
    c->label_count++;
}

static void emit_jump(Ctx *c, OpCode op, const char *label, int line)
{
    if (!label) { error(c, line, "Jump without a target label%s", ""); return; }

    int at = emit(c, op, -1, line);
    if (at < 0) return;

    if (c->fixup_count == c->fixup_cap) {
        int cap = c->fixup_cap ? c->fixup_cap * 2 : 16;
        Fixup *items = realloc(c->fixups, (size_t)cap * sizeof(Fixup));
        if (!items) { error(c, line, "Out of memory during code generation%s", ""); return; }
        c->fixups = items; c->fixup_cap = cap;
    }
    c->fixups[c->fixup_count].code_index = at;
    c->fixups[c->fixup_count].label = label;
    c->fixups[c->fixup_count].line = line;
    c->fixup_count++;
}

static int declare_visitor(const ASTNode *node, int depth, void *user)
{
    (void)depth;
    Ctx *c = user;
    if (node->type == AST_DECLARATION)
        if (vm_program_slot(c->prog, node->data.declaration.name) < 0)
            error(c, node->line, "Out of memory during code generation%s", "");
    return c->failed;
}

int codegen_generate(const IRProgram *ir, const ASTNode *ast,
                     VMProgram *out, DiagnosticList *diags)
{
    if (!ir || !out || !diags) return 0;

    Ctx c;
    memset(&c, 0, sizeof(c));
    c.prog = out;
    c.diags = diags;

    if (ast) ast_walk(ast, declare_visitor, &c);

    for (int i = 0; i < ir->count && !c.failed; i++) {
        const IRInstruction *in = &ir->instructions[i];
        const char *op = in->op ? in->op : "";
        OpCode code;

        if (strcmp(op, "=") == 0) {
            push_operand(&c, in->arg1, in->line);
            store_result(&c, in->result, in->line);
        } else if (strcmp(op, "label") == 0) {
            if (!in->result) error(&c, in->line, "Label without a name%s", "");
            else define_label(&c, in->result, in->line);
        } else if (strcmp(op, "goto") == 0) {
            emit_jump(&c, OP_JMP, in->result, in->line);
        } else if (strcmp(op, "ifFalse") == 0) {
            push_operand(&c, in->arg1, in->line);
            emit_jump(&c, OP_JZ, in->result, in->line);
        } else if (in->arg2 && binary_opcode(op, &code)) {
            push_operand(&c, in->arg1, in->line);
            push_operand(&c, in->arg2, in->line);
            emit(&c, code, 0, in->line);
            store_result(&c, in->result, in->line);
        } else if (!in->arg2 && (strcmp(op, "-") == 0 || strcmp(op, "!") == 0)) {
            push_operand(&c, in->arg1, in->line);
            emit(&c, strcmp(op, "-") == 0 ? OP_NEG : OP_NOT, 0, in->line);
            store_result(&c, in->result, in->line);
        } else {
            error(&c, in->line, "Unknown IR operation '%s'", op);
        }
    }

    if (!c.failed) emit(&c, OP_HALT, 0, 0);

    /* resolve jump targets */
    for (int i = 0; i < c.fixup_count && !c.failed; i++) {
        int found = 0;
        for (int j = 0; j < c.label_count; j++) {
            if (strcmp(c.labels[j].name, c.fixups[i].label) == 0) {
                out->code[c.fixups[i].code_index].operand = c.labels[j].address;
                found = 1;
                break;
            }
        }
        if (!found) error(&c, c.fixups[i].line, "Jump to undefined label '%s'", c.fixups[i].label);
    }

    for (int i = 0; i < c.label_count; i++) free(c.labels[i].name);
    free(c.labels);
    free(c.fixups);

    return !c.failed;
}