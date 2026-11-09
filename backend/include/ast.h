#ifndef AST_H
#define AST_H

/*
 * ast.h - Abstract Syntax Tree (SHARED INTERFACE - Person 2 and 3 read it).
 *
 * Field names follow the project specification. Differences / additions:
 *   - `capacity` on program/block (dynamic arrays)
 *   - `height` on every node (used by the parser to bound recursion depth)
 *   - `int a = 1, b = 2;` becomes TWO AST_DECLARATION nodes (one per name)
 *   - parentheses do not create nodes; precedence is encoded by the tree shape
 *   - `-2147483648` is folded by the parser into one AST_INTEGER node
 *
 * Ownership: a parent owns its children. ast_free(root) frees the whole tree.
 * Constructors COPY strings, and TAKE OWNERSHIP of child nodes (they are freed
 * for you even if the constructor fails and returns NULL).
 *
 * Operators are stored as text: binary "+ - * / < > <= >= == != && ||",
 * unary "-" and "!".
 */

#include <stdint.h>
#include <stdio.h>

#include "strbuf.h"

#define AST_MAX_DEPTH 1000 /* parser rejects trees taller than this */

typedef enum {
    AST_PROGRAM,
    AST_DECLARATION,
    AST_ASSIGNMENT,
    AST_BINARY_EXPR,
    AST_UNARY_EXPR,
    AST_INTEGER,
    AST_IDENTIFIER,
    AST_IF,
    AST_WHILE,
    AST_BLOCK
} ASTNodeType;

typedef struct ASTNode ASTNode;

struct ASTNode {
    ASTNodeType type;
    int line;   /* 1-based; binary nodes point at the operator */
    int column; /* 1-based */
    int height; /* 1 for a leaf; internal use */

    union {
        struct {
            ASTNode **statements;
            int count;
            int capacity;
        } program;

        struct {
            char *name;
            char *data_type;      /* "int" */
            ASTNode *initializer; /* NULL when there is no "= expr" */
        } declaration;

        struct {
            char *name;
            ASTNode *value;
        } assignment;

        struct {
            char *operator;
            ASTNode *left;
            ASTNode *right;
        } binary;

        struct {
            char *operator;
            ASTNode *operand;
        } unary;

        struct {
            int32_t value;
        } integer;

        struct {
            char *name;
        } identifier;

        struct {
            ASTNode *condition;
            ASTNode *then_branch;
            ASTNode *else_branch; /* NULL when there is no else */
        } if_statement;

        struct {
            ASTNode *condition;
            ASTNode *body;
        } while_statement;

        struct {
            ASTNode **statements;
            int count;
            int capacity;
        } block;
    } data;
};

/* ---------- creation (return NULL on out-of-memory) ---------- */
ASTNode *ast_new_program(int line, int column);
ASTNode *ast_new_block(int line, int column);
/* Appends to a PROGRAM or BLOCK node. Returns 1 on success; on failure (0)
 * the caller still owns `stmt`. */
int ast_append_statement(ASTNode *container, ASTNode *stmt);

ASTNode *ast_new_declaration(const char *name, const char *data_type, ASTNode *initializer,
                             int line, int column);
ASTNode *ast_new_assignment(const char *name, ASTNode *value, int line, int column);
ASTNode *ast_new_binary(const char *op, ASTNode *left, ASTNode *right, int line, int column);
ASTNode *ast_new_unary(const char *op, ASTNode *operand, int line, int column);
ASTNode *ast_new_integer(int32_t value, int line, int column);
ASTNode *ast_new_identifier(const char *name, int line, int column);
ASTNode *ast_new_if(ASTNode *condition, ASTNode *then_branch, ASTNode *else_branch,
                    int line, int column);
ASTNode *ast_new_while(ASTNode *condition, ASTNode *body, int line, int column);

/* ---------- destruction ---------- */
void ast_free(ASTNode *node); /* recursive; NULL-safe */

/* ---------- traversal ---------- */
const char *ast_type_name(ASTNodeType type); /* "Program", "BinaryExpression", ... */

/* Generic child access, in source order. `role` (optional) receives a label
 * such as "left", "right", "condition", "then", "else", "body", "value",
 * "initializer", "operand" or "statement". */
int ast_child_count(const ASTNode *node);
ASTNode *ast_child_at(const ASTNode *node, int index, const char **role);

/* Pre-order walk. The visitor returns 0 to continue, non-zero to stop the whole
 * walk. Returns 1 if the walk was stopped early, 0 otherwise. */
typedef int (*ASTVisitor)(const ASTNode *node, int depth, void *user);
int ast_walk(const ASTNode *root, ASTVisitor visitor, void *user);

/* ---------- output ---------- */
void ast_print(const ASTNode *root, FILE *out); /* indented text tree */

/* Compact LISP-style text used by tests, e.g. "(+ 2 (* 3 4))". */
void ast_write_sexpr(const ASTNode *root, StrBuf *sb);

/* JSON tree. Every node: {"role"?, "type", "label", "line", "column",
 * "children":[...]} plus "name" / "dataType" / "operator" / "value" where
 * relevant. */
void ast_write_json(const ASTNode *root, StrBuf *sb);
char *ast_to_json(const ASTNode *root); /* caller frees; NULL on OOM */

#endif /* AST_H */
