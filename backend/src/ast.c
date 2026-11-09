#include "ast.h"

#include <stdlib.h>
#include <string.h>

/* ---------- construction helpers ---------- */

static int height_of(const ASTNode *n) { return n ? n->height : 0; }

static int max_int(int a, int b) { return a > b ? a : b; }

static ASTNode *alloc_node(ASTNodeType type, int line, int column) {
    ASTNode *n = calloc(1, sizeof *n);
    if (!n) return NULL;
    n->type = type;
    n->line = line;
    n->column = column;
    n->height = 1;
    return n;
}

ASTNode *ast_new_program(int line, int column) {
    return alloc_node(AST_PROGRAM, line, column);
}

ASTNode *ast_new_block(int line, int column) {
    return alloc_node(AST_BLOCK, line, column);
}

int ast_append_statement(ASTNode *container, ASTNode *stmt) {
    if (!container || !stmt) return 0;
    ASTNode ***arr;
    int *count;
    int *cap;
    if (container->type == AST_PROGRAM) {
        arr = &container->data.program.statements;
        count = &container->data.program.count;
        cap = &container->data.program.capacity;
    } else if (container->type == AST_BLOCK) {
        arr = &container->data.block.statements;
        count = &container->data.block.count;
        cap = &container->data.block.capacity;
    } else {
        return 0;
    }

    if (*count == *cap) {
        int ncap = *cap ? *cap * 2 : 8;
        ASTNode **na = realloc(*arr, (size_t)ncap * sizeof **arr);
        if (!na) return 0;
        *arr = na;
        *cap = ncap;
    }
    (*arr)[(*count)++] = stmt;
    container->height = max_int(container->height, stmt->height + 1);
    return 1;
}

ASTNode *ast_new_declaration(const char *name, const char *data_type, ASTNode *initializer,
                             int line, int column) {
    ASTNode *n = alloc_node(AST_DECLARATION, line, column);
    char *nm = str_dup(name);
    char *ty = str_dup(data_type);
    if (!n || !nm || !ty) {
        free(n);
        free(nm);
        free(ty);
        ast_free(initializer);
        return NULL;
    }
    n->data.declaration.name = nm;
    n->data.declaration.data_type = ty;
    n->data.declaration.initializer = initializer;
    n->height = 1 + height_of(initializer);
    return n;
}

ASTNode *ast_new_assignment(const char *name, ASTNode *value, int line, int column) {
    ASTNode *n = alloc_node(AST_ASSIGNMENT, line, column);
    char *nm = str_dup(name);
    if (!n || !nm || !value) {
        free(n);
        free(nm);
        ast_free(value);
        return NULL;
    }
    n->data.assignment.name = nm;
    n->data.assignment.value = value;
    n->height = 1 + value->height;
    return n;
}

ASTNode *ast_new_binary(const char *op, ASTNode *left, ASTNode *right, int line, int column) {
    ASTNode *n = alloc_node(AST_BINARY_EXPR, line, column);
    char *o = str_dup(op);
    if (!n || !o || !left || !right) {
        free(n);
        free(o);
        ast_free(left);
        ast_free(right);
        return NULL;
    }
    n->data.binary.operator = o;
    n->data.binary.left = left;
    n->data.binary.right = right;
    n->height = 1 + max_int(left->height, right->height);
    return n;
}

ASTNode *ast_new_unary(const char *op, ASTNode *operand, int line, int column) {
    ASTNode *n = alloc_node(AST_UNARY_EXPR, line, column);
    char *o = str_dup(op);
    if (!n || !o || !operand) {
        free(n);
        free(o);
        ast_free(operand);
        return NULL;
    }
    n->data.unary.operator = o;
    n->data.unary.operand = operand;
    n->height = 1 + operand->height;
    return n;
}

ASTNode *ast_new_integer(int32_t value, int line, int column) {
    ASTNode *n = alloc_node(AST_INTEGER, line, column);
    if (!n) return NULL;
    n->data.integer.value = value;
    return n;
}

ASTNode *ast_new_identifier(const char *name, int line, int column) {
    ASTNode *n = alloc_node(AST_IDENTIFIER, line, column);
    char *nm = str_dup(name);
    if (!n || !nm) {
        free(n);
        free(nm);
        return NULL;
    }
    n->data.identifier.name = nm;
    return n;
}

ASTNode *ast_new_if(ASTNode *condition, ASTNode *then_branch, ASTNode *else_branch,
                    int line, int column) {
    ASTNode *n = alloc_node(AST_IF, line, column);
    if (!n || !condition || !then_branch) {
        free(n);
        ast_free(condition);
        ast_free(then_branch);
        ast_free(else_branch);
        return NULL;
    }
    n->data.if_statement.condition = condition;
    n->data.if_statement.then_branch = then_branch;
    n->data.if_statement.else_branch = else_branch;
    n->height = 1 + max_int(condition->height,
                            max_int(then_branch->height, height_of(else_branch)));
    return n;
}

ASTNode *ast_new_while(ASTNode *condition, ASTNode *body, int line, int column) {
    ASTNode *n = alloc_node(AST_WHILE, line, column);
    if (!n || !condition || !body) {
        free(n);
        ast_free(condition);
        ast_free(body);
        return NULL;
    }
    n->data.while_statement.condition = condition;
    n->data.while_statement.body = body;
    n->height = 1 + max_int(condition->height, body->height);
    return n;
}

/* ---------- destruction ---------- */

void ast_free(ASTNode *node) {
    if (!node) return;

    int n = ast_child_count(node);
    for (int i = 0; i < n; i++) ast_free(ast_child_at(node, i, NULL));

    switch (node->type) {
        case AST_PROGRAM:
            free(node->data.program.statements);
            break;
        case AST_BLOCK:
            free(node->data.block.statements);
            break;
        case AST_DECLARATION:
            free(node->data.declaration.name);
            free(node->data.declaration.data_type);
            break;
        case AST_ASSIGNMENT:
            free(node->data.assignment.name);
            break;
        case AST_BINARY_EXPR:
            free(node->data.binary.operator);
            break;
        case AST_UNARY_EXPR:
            free(node->data.unary.operator);
            break;
        case AST_IDENTIFIER:
            free(node->data.identifier.name);
            break;
        case AST_INTEGER:
        case AST_IF:
        case AST_WHILE:
            break;
    }
    free(node);
}

/* ---------- traversal ---------- */

const char *ast_type_name(ASTNodeType type) {
    switch (type) {
        case AST_PROGRAM:     return "Program";
        case AST_DECLARATION: return "Declaration";
        case AST_ASSIGNMENT:  return "Assignment";
        case AST_BINARY_EXPR: return "BinaryExpression";
        case AST_UNARY_EXPR:  return "UnaryExpression";
        case AST_INTEGER:     return "IntegerLiteral";
        case AST_IDENTIFIER:  return "Identifier";
        case AST_IF:          return "IfStatement";
        case AST_WHILE:       return "WhileStatement";
        case AST_BLOCK:       return "Block";
    }
    return "Unknown";
}

int ast_child_count(const ASTNode *n) {
    if (!n) return 0;
    switch (n->type) {
        case AST_PROGRAM:     return n->data.program.count;
        case AST_BLOCK:       return n->data.block.count;
        case AST_DECLARATION: return n->data.declaration.initializer ? 1 : 0;
        case AST_ASSIGNMENT:  return 1;
        case AST_BINARY_EXPR: return 2;
        case AST_UNARY_EXPR:  return 1;
        case AST_IF:          return n->data.if_statement.else_branch ? 3 : 2;
        case AST_WHILE:       return 2;
        case AST_INTEGER:
        case AST_IDENTIFIER:  return 0;
    }
    return 0;
}

ASTNode *ast_child_at(const ASTNode *n, int index, const char **role) {
    const char *r = "";
    ASTNode *child = NULL;
    if (n && index >= 0 && index < ast_child_count(n)) {
        switch (n->type) {
            case AST_PROGRAM:
                r = "statement";
                child = n->data.program.statements[index];
                break;
            case AST_BLOCK:
                r = "statement";
                child = n->data.block.statements[index];
                break;
            case AST_DECLARATION:
                r = "initializer";
                child = n->data.declaration.initializer;
                break;
            case AST_ASSIGNMENT:
                r = "value";
                child = n->data.assignment.value;
                break;
            case AST_BINARY_EXPR:
                r = index == 0 ? "left" : "right";
                child = index == 0 ? n->data.binary.left : n->data.binary.right;
                break;
            case AST_UNARY_EXPR:
                r = "operand";
                child = n->data.unary.operand;
                break;
            case AST_IF:
                if (index == 0) {
                    r = "condition";
                    child = n->data.if_statement.condition;
                } else if (index == 1) {
                    r = "then";
                    child = n->data.if_statement.then_branch;
                } else {
                    r = "else";
                    child = n->data.if_statement.else_branch;
                }
                break;
            case AST_WHILE:
                r = index == 0 ? "condition" : "body";
                child = index == 0 ? n->data.while_statement.condition
                                   : n->data.while_statement.body;
                break;
            case AST_INTEGER:
            case AST_IDENTIFIER:
                break;
        }
    }
    if (role) *role = r;
    return child;
}

static int walk_rec(const ASTNode *n, int depth, ASTVisitor visitor, void *user) {
    if (visitor(n, depth, user)) return 1;
    int count = ast_child_count(n);
    for (int i = 0; i < count; i++) {
        if (walk_rec(ast_child_at(n, i, NULL), depth + 1, visitor, user)) return 1;
    }
    return 0;
}

int ast_walk(const ASTNode *root, ASTVisitor visitor, void *user) {
    if (!root || !visitor) return 0;
    return walk_rec(root, 0, visitor, user);
}

/* ---------- printing ---------- */

/* Short human-readable label: "int x", "x =", "+", "5", "x" ... */
static void node_label(const ASTNode *n, char *buf, size_t size) {
    buf[0] = '\0';
    switch (n->type) {
        case AST_DECLARATION:
            snprintf(buf, size, "%s %s", n->data.declaration.data_type, n->data.declaration.name);
            break;
        case AST_ASSIGNMENT:
            snprintf(buf, size, "%s =", n->data.assignment.name);
            break;
        case AST_BINARY_EXPR:
            snprintf(buf, size, "%s", n->data.binary.operator);
            break;
        case AST_UNARY_EXPR:
            snprintf(buf, size, "%s", n->data.unary.operator);
            break;
        case AST_INTEGER:
            snprintf(buf, size, "%d", (int)n->data.integer.value);
            break;
        case AST_IDENTIFIER:
            snprintf(buf, size, "%s", n->data.identifier.name);
            break;
        case AST_PROGRAM:
        case AST_IF:
        case AST_WHILE:
        case AST_BLOCK:
            break;
    }
}

static void print_rec(const ASTNode *n, const char *role, int depth, FILE *out) {
    char label[128];
    node_label(n, label, sizeof label);
    for (int i = 0; i < depth; i++) fputs("  ", out);
    if (role && role[0] && strcmp(role, "statement") != 0) fprintf(out, "%s: ", role);
    fprintf(out, "%s", ast_type_name(n->type));
    if (label[0]) fprintf(out, " '%s'", label);
    fprintf(out, "  (line %d, col %d)\n", n->line, n->column);

    int count = ast_child_count(n);
    for (int i = 0; i < count; i++) {
        const char *child_role = "";
        ASTNode *child = ast_child_at(n, i, &child_role);
        print_rec(child, child_role, depth + 1, out);
    }
}

void ast_print(const ASTNode *root, FILE *out) {
    if (!root) {
        fputs("(empty AST)\n", out);
        return;
    }
    print_rec(root, NULL, 0, out);
}

/* ---------- S-expression (for tests) ---------- */

void ast_write_sexpr(const ASTNode *n, StrBuf *sb) {
    if (!n) {
        sb_append(sb, "()");
        return;
    }
    switch (n->type) {
        case AST_INTEGER:
            sb_appendf(sb, "%d", (int)n->data.integer.value);
            return;
        case AST_IDENTIFIER:
            sb_append(sb, n->data.identifier.name);
            return;
        case AST_PROGRAM:
            sb_append(sb, "(program");
            break;
        case AST_BLOCK:
            sb_append(sb, "(block");
            break;
        case AST_DECLARATION:
            sb_appendf(sb, "(decl %s", n->data.declaration.name);
            break;
        case AST_ASSIGNMENT:
            sb_appendf(sb, "(= %s", n->data.assignment.name);
            break;
        case AST_BINARY_EXPR:
            sb_appendf(sb, "(%s", n->data.binary.operator);
            break;
        case AST_UNARY_EXPR:
            sb_append(sb, strcmp(n->data.unary.operator, "-") == 0 ? "(neg" : "(not");
            break;
        case AST_IF:
            sb_append(sb, "(if");
            break;
        case AST_WHILE:
            sb_append(sb, "(while");
            break;
    }
    int count = ast_child_count(n);
    for (int i = 0; i < count; i++) {
        sb_append_char(sb, ' ');
        ast_write_sexpr(ast_child_at(n, i, NULL), sb);
    }
    sb_append_char(sb, ')');
}

/* ---------- JSON ---------- */

static void json_rec(const ASTNode *n, const char *role, StrBuf *sb) {
    char label[128];
    node_label(n, label, sizeof label);

    sb_append_char(sb, '{');
    if (role && role[0]) {
        sb_append(sb, "\"role\":");
        sb_append_json_string(sb, role);
        sb_append_char(sb, ',');
    }
    sb_append(sb, "\"type\":");
    sb_append_json_string(sb, ast_type_name(n->type));
    sb_append(sb, ",\"label\":");
    sb_append_json_string(sb, label);
    sb_appendf(sb, ",\"line\":%d,\"column\":%d", n->line, n->column);

    switch (n->type) {
        case AST_DECLARATION:
            sb_append(sb, ",\"name\":");
            sb_append_json_string(sb, n->data.declaration.name);
            sb_append(sb, ",\"dataType\":");
            sb_append_json_string(sb, n->data.declaration.data_type);
            break;
        case AST_ASSIGNMENT:
            sb_append(sb, ",\"name\":");
            sb_append_json_string(sb, n->data.assignment.name);
            break;
        case AST_BINARY_EXPR:
            sb_append(sb, ",\"operator\":");
            sb_append_json_string(sb, n->data.binary.operator);
            break;
        case AST_UNARY_EXPR:
            sb_append(sb, ",\"operator\":");
            sb_append_json_string(sb, n->data.unary.operator);
            break;
        case AST_INTEGER:
            sb_appendf(sb, ",\"value\":%d", (int)n->data.integer.value);
            break;
        case AST_IDENTIFIER:
            sb_append(sb, ",\"name\":");
            sb_append_json_string(sb, n->data.identifier.name);
            break;
        case AST_PROGRAM:
        case AST_IF:
        case AST_WHILE:
        case AST_BLOCK:
            break;
    }

    sb_append(sb, ",\"children\":[");
    int count = ast_child_count(n);
    for (int i = 0; i < count; i++) {
        const char *child_role = "";
        ASTNode *child = ast_child_at(n, i, &child_role);
        if (i > 0) sb_append_char(sb, ',');
        json_rec(child, child_role, sb);
    }
    sb_append(sb, "]}");
}

void ast_write_json(const ASTNode *root, StrBuf *sb) {
    if (!root) {
        sb_append(sb, "null");
        return;
    }
    json_rec(root, NULL, sb);
}

char *ast_to_json(const ASTNode *root) {
    StrBuf sb;
    sb_init(&sb);
    ast_write_json(root, &sb);
    return sb_detach(&sb);
}
