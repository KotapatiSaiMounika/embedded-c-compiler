#include "parser.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Recursive descent parser. One function per grammar rule.
 *
 * Error handling convention:
 *   - every parse_* function returns NULL (or 0) on failure, after reporting
 *     exactly one diagnostic, and frees whatever it had built so far;
 *   - the statement loops (program / block) then call synchronize() to skip to
 *     a safe place ("panic mode recovery") and keep parsing, so later errors
 *     are still found;
 *   - parser_parse() returns NULL if even one error was reported.
 */

typedef struct {
    const TokenArray *tokens;
    int pos;
    DiagnosticList *diags;
    int depth;  /* current recursion depth */
    int errors; /* syntax errors reported */
    int fatal;  /* out of memory / nesting too deep: stop parsing */
    int last_err_line; /* position of the last reported error (to avoid duplicates) */
    int last_err_col;
    int skip_else;     /* a failed 'if' was just seen: its 'else' is not a new error */
} Parser;

/* ============================ token helpers ============================ */

static const Token *cur(const Parser *p) { return &p->tokens->items[p->pos]; }

static const Token *prev(const Parser *p) {
    return &p->tokens->items[p->pos > 0 ? p->pos - 1 : 0];
}

static int check(const Parser *p, TokenType t) { return cur(p)->type == t; }

static int at_end(const Parser *p) { return cur(p)->type == TOKEN_EOF; }

static const Token *advance(Parser *p) {
    const Token *t = cur(p);
    if (t->type != TOKEN_EOF) p->pos++; /* never moves past EOF */
    return t;
}

static int match(Parser *p, TokenType t) {
    if (check(p, t)) {
        advance(p);
        return 1;
    }
    return 0;
}

/* Describes a token for messages: 'x' or "end of input". */
static void describe(const Token *t, char *buf, size_t size) {
    if (t->type == TOKEN_EOF) {
        snprintf(buf, size, "end of input");
    } else {
        snprintf(buf, size, "'%.40s'", t->lexeme);
    }
}

/* ============================ error reporting ============================ */

static void error_at(Parser *p, int line, int col, const char *fmt, ...) DIAG_PRINTF(4, 5);

static void error_at(Parser *p, int line, int col, const char *fmt, ...) {
    char msg[DIAG_MESSAGE_SIZE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    /* One error per token: recovery can land on the token that already failed. */
    if (p->errors > 0 && line == p->last_err_line && col == p->last_err_col) return;
    p->last_err_line = line;
    p->last_err_col = col;
    diag_list_add(p->diags, DIAG_STAGE_SYNTAX, DIAG_ERROR, line, col, "%s", msg);
    p->errors++;
}

static ASTNode *oom(Parser *p) {
    if (!p->fatal) error_at(p, cur(p)->line, cur(p)->column, "Out of memory");
    p->fatal = 1;
    return NULL;
}

/* "Expected <what>, found <token>" at the current token. */
static const Token *expect(Parser *p, TokenType type, const char *what) {
    if (check(p, type)) return advance(p);
    char found[64];
    describe(cur(p), found, sizeof found);
    error_at(p, cur(p)->line, cur(p)->column, "Expected %s, found %s", what, found);
    return NULL;
}

/* A missing ';' is reported just AFTER the previous token (where it belongs). */
static int expect_semicolon(Parser *p, const char *after) {
    if (match(p, TOKEN_SEMICOLON)) return 1;
    const Token *last = prev(p);
    int col = last->column + (int)strlen(last->lexeme);
    error_at(p, last->line, col, "Expected ';' after %s", after);
    return 0;
}

static int is_type_keyword(const char *s) {
    static const char *const types[] = {"float", "double", "char", "short", "long",
                                        "unsigned", "signed", "void"};
    for (size_t i = 0; i < sizeof types / sizeof types[0]; i++) {
        if (strcmp(s, types[i]) == 0) return 1;
    }
    return 0;
}

/* Reports a reserved C word that is outside the subset. Does not consume it. */
static void report_unsupported(Parser *p, const Token *t) {
    const char *w = t->lexeme;
    if (strcmp(w, "for") == 0) {
        error_at(p, t->line, t->column,
                 "'for' loops are not supported in this C subset (use 'while')");
    } else if (strcmp(w, "do") == 0) {
        error_at(p, t->line, t->column,
                 "'do-while' loops are not supported in this C subset (use 'while')");
    } else if (is_type_keyword(w)) {
        error_at(p, t->line, t->column,
                 "Type '%s' is not supported in this C subset (only 'int' is available)", w);
    } else {
        error_at(p, t->line, t->column, "'%s' is not supported in this C subset", w);
    }
}

/* ============================ recovery ============================ */

/* Skips a balanced (...) or {...} group. Current token must be `open`. */
static void skip_balanced(Parser *p, TokenType open, TokenType close) {
    int level = 0;
    while (!at_end(p)) {
        TokenType t = advance(p)->type;
        if (t == open) level++;
        if (t == close && --level == 0) return;
    }
}

/* After reporting an unsupported statement keyword (for, break, return,
 * float ...), skip the whole construct so its pieces do not cause extra errors. */
static void skip_unsupported_statement(Parser *p) {
    advance(p); /* the keyword */
    while (!at_end(p) && !check(p, TOKEN_SEMICOLON) && !check(p, TOKEN_LBRACE) &&
           !check(p, TOKEN_RBRACE)) {
        if (check(p, TOKEN_LPAREN)) {
            skip_balanced(p, TOKEN_LPAREN, TOKEN_RPAREN);
        } else {
            advance(p);
        }
    }
    if (check(p, TOKEN_LBRACE)) {
        skip_balanced(p, TOKEN_LBRACE, TOKEN_RBRACE);
        match(p, TOKEN_SEMICOLON);
    } else {
        match(p, TOKEN_SEMICOLON);
    }
}

/* Panic-mode recovery: skip tokens until just after a ';' or until a token that
 * can start a statement. `start` is where the failed statement began; at least
 * one token is always consumed so the caller's loop cannot spin forever. */
static void synchronize(Parser *p, int start) {
    if (p->fatal) return;
    if (p->pos == start && !at_end(p)) advance(p);
    while (!at_end(p)) {
        if (p->pos > 0 && (prev(p)->type == TOKEN_SEMICOLON || prev(p)->type == TOKEN_RBRACE)) return;
        switch (cur(p)->type) {
            case TOKEN_INT:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_LBRACE:
            case TOKEN_RBRACE:
            case TOKEN_UNSUPPORTED_KEYWORD:
                return;
            default:
                break;
        }
        advance(p);
    }
}

/* ============================ depth guard ============================ */

static int enter(Parser *p) {
    if (p->depth >= PARSER_MAX_DEPTH) {
        if (!p->fatal) {
            error_at(p, cur(p)->line, cur(p)->column,
                     "Program is nested too deeply (limit is %d levels)", PARSER_MAX_DEPTH);
        }
        p->fatal = 1;
        return 0;
    }
    p->depth++;
    return 1;
}

/* ============================ expressions ============================ */

typedef ASTNode *(*ParseFn)(Parser *);

static ASTNode *parse_expression(Parser *p);
static ASTNode *parse_logical_and(Parser *p);
static ASTNode *parse_equality(Parser *p);
static ASTNode *parse_relational(Parser *p);
static ASTNode *parse_additive(Parser *p);
static ASTNode *parse_multiplicative(Parser *p);
static ASTNode *parse_unary(Parser *p);
static ASTNode *parse_primary(Parser *p);

static int is_expr_start(TokenType t) {
    return t == TOKEN_INTEGER_LITERAL || t == TOKEN_IDENTIFIER || t == TOKEN_LPAREN ||
           t == TOKEN_MINUS || t == TOKEN_NOT;
}

/* After an operator/'=' there must be an expression: "Expected expression after '='". */
static int require_expr(Parser *p, const char *after) {
    /* an unsupported keyword (sizeof ...) is reported by parse_primary with a better message */
    if (is_expr_start(cur(p)->type) || check(p, TOKEN_UNSUPPORTED_KEYWORD)) return 1;
    error_at(p, cur(p)->line, cur(p)->column, "Expected expression after '%s'", after);
    return 0;
}

/* Converts digits to a number. Returns 0 if the value exceeds 2147483648
 * (the largest magnitude that can still be negated into an int). */
static int literal_value(const char *digits, int64_t *out) {
    int64_t v = 0;
    for (const char *s = digits; *s; s++) {
        v = v * 10 + (*s - '0');
        if (v > 2147483648LL) return 0;
    }
    *out = v;
    return 1;
}

/* One left-associative binary level:  level -> next (op next)*  */
static ASTNode *parse_binary_level(Parser *p, ParseFn next, const TokenType *ops, int nops) {
    ASTNode *left = next(p);
    if (!left) return NULL;

    for (;;) {
        const Token *op = cur(p);
        int is_op = 0;
        for (int i = 0; i < nops; i++) {
            if (op->type == ops[i]) is_op = 1;
        }
        if (!is_op) break;

        advance(p);
        if (!require_expr(p, op->lexeme)) {
            ast_free(left);
            return NULL;
        }
        ASTNode *right = next(p);
        if (!right) {
            ast_free(left);
            return NULL;
        }
        ASTNode *bin = ast_new_binary(op->lexeme, left, right, op->line, op->column);
        if (!bin) return oom(p); /* ast_new_binary already freed left and right */

        if (bin->height > AST_MAX_DEPTH) {
            error_at(p, op->line, op->column,
                     "Expression is too long or too deeply nested (limit is %d levels)",
                     AST_MAX_DEPTH);
            ast_free(bin);
            return NULL;
        }
        left = bin;
    }
    return left;
}

static ASTNode *parse_expression(Parser *p) {
    static const TokenType ops[] = {TOKEN_OR};
    return parse_binary_level(p, parse_logical_and, ops, 1);
}

static ASTNode *parse_logical_and(Parser *p) {
    static const TokenType ops[] = {TOKEN_AND};
    return parse_binary_level(p, parse_equality, ops, 1);
}

static ASTNode *parse_equality(Parser *p) {
    static const TokenType ops[] = {TOKEN_EQUAL, TOKEN_NOT_EQUAL};
    return parse_binary_level(p, parse_relational, ops, 2);
}

static ASTNode *parse_relational(Parser *p) {
    static const TokenType ops[] = {TOKEN_LESS, TOKEN_GREATER, TOKEN_LESS_EQUAL,
                                    TOKEN_GREATER_EQUAL};
    return parse_binary_level(p, parse_additive, ops, 4);
}

static ASTNode *parse_additive(Parser *p) {
    static const TokenType ops[] = {TOKEN_PLUS, TOKEN_MINUS};
    return parse_binary_level(p, parse_multiplicative, ops, 2);
}

static ASTNode *parse_multiplicative(Parser *p) {
    static const TokenType ops[] = {TOKEN_MULTIPLY, TOKEN_DIVIDE};
    return parse_binary_level(p, parse_unary, ops, 2);
}

static ASTNode *parse_unary_inner(Parser *p) {
    const Token *t = cur(p);
    if (t->type != TOKEN_MINUS && t->type != TOKEN_NOT) return parse_primary(p);

    advance(p);

    /* "-2147483648" is the one literal that only fits once negated. */
    if (t->type == TOKEN_MINUS && check(p, TOKEN_INTEGER_LITERAL)) {
        int64_t v;
        if (literal_value(cur(p)->lexeme, &v) && v == 2147483648LL) {
            advance(p);
            ASTNode *n = ast_new_integer(INT32_MIN, t->line, t->column);
            return n ? n : oom(p);
        }
    }

    if (!require_expr(p, t->lexeme)) return NULL;
    ASTNode *operand = parse_unary(p);
    if (!operand) return NULL;
    ASTNode *u = ast_new_unary(t->lexeme, operand, t->line, t->column);
    return u ? u : oom(p);
}

static ASTNode *parse_unary(Parser *p) {
    if (!enter(p)) return NULL;
    ASTNode *n = parse_unary_inner(p);
    p->depth--;
    return n;
}

static ASTNode *parse_primary(Parser *p) {
    const Token *t = cur(p);
    switch (t->type) {
        case TOKEN_INTEGER_LITERAL: {
            int64_t v;
            if (!literal_value(t->lexeme, &v) || v > INT32_MAX) {
                error_at(p, t->line, t->column,
                         "Integer literal '%.20s' is out of range for a 32-bit int (maximum 2147483647)",
                         t->lexeme);
                return NULL;
            }
            advance(p);
            ASTNode *n = ast_new_integer((int32_t)v, t->line, t->column);
            return n ? n : oom(p);
        }
        case TOKEN_IDENTIFIER: {
            advance(p);
            ASTNode *n = ast_new_identifier(t->lexeme, t->line, t->column);
            return n ? n : oom(p);
        }
        case TOKEN_LPAREN: {
            advance(p);
            if (!require_expr(p, "(")) return NULL;
            ASTNode *inner = parse_expression(p);
            if (!inner) return NULL;
            if (!check(p, TOKEN_RPAREN)) {
                error_at(p, cur(p)->line, cur(p)->column,
                         "Expected ')' to close the '(' at line %d, column %d", t->line,
                         t->column);
                ast_free(inner);
                return NULL;
            }
            advance(p);
            return inner; /* parentheses leave no node; the tree shape is the grouping */
        }
        case TOKEN_UNSUPPORTED_KEYWORD:
            report_unsupported(p, t);
            advance(p);
            return NULL;
        default: {
            char found[64];
            describe(t, found, sizeof found);
            error_at(p, t->line, t->column, "Expected expression, found %s", found);
            return NULL;
        }
    }
}

/* ============================ statements ============================ */

static ASTNode *parse_statement(Parser *p);
static int parse_item(Parser *p, ASTNode *container);

/* '(' expression ')'  used by if and while. */
static ASTNode *parse_condition(Parser *p, const char *keyword) {
    char what[48];
    snprintf(what, sizeof what, "'(' after '%s'", keyword);
    if (!expect(p, TOKEN_LPAREN, what)) return NULL;
    if (!require_expr(p, "(")) return NULL;

    ASTNode *cond = parse_expression(p);
    if (!cond) return NULL;

    if (!check(p, TOKEN_RPAREN)) {
        if (check(p, TOKEN_ASSIGN)) {
            error_at(p, cur(p)->line, cur(p)->column,
                     "Expected ')' after condition, found '=' (use '==' to compare values)");
        } else {
            char found[64];
            describe(cur(p), found, sizeof found);
            error_at(p, cur(p)->line, cur(p)->column, "Expected ')' after condition, found %s",
                     found);
        }
        ast_free(cond);
        return NULL;
    }
    advance(p);
    return cond;
}

static ASTNode *parse_if(Parser *p) {
    const Token *kw = advance(p); /* 'if' */
    ASTNode *cond = parse_condition(p, "if");
    if (!cond) {
        p->skip_else = 1;
        return NULL;
    }

    ASTNode *then_branch = parse_statement(p);
    if (!then_branch) {
        p->skip_else = 1;
        ast_free(cond);
        return NULL;
    }

    ASTNode *else_branch = NULL;
    if (match(p, TOKEN_ELSE)) {
        else_branch = parse_statement(p);
        if (!else_branch) {
            ast_free(cond);
            ast_free(then_branch);
            return NULL;
        }
    }
    ASTNode *n = ast_new_if(cond, then_branch, else_branch, kw->line, kw->column);
    return n ? n : oom(p);
}

static ASTNode *parse_while(Parser *p) {
    const Token *kw = advance(p); /* 'while' */
    ASTNode *cond = parse_condition(p, "while");
    if (!cond) return NULL;

    ASTNode *body = parse_statement(p);
    if (!body) {
        ast_free(cond);
        return NULL;
    }
    ASTNode *n = ast_new_while(cond, body, kw->line, kw->column);
    return n ? n : oom(p);
}

static ASTNode *parse_block(Parser *p) {
    const Token *open = advance(p); /* '{' */
    ASTNode *block = ast_new_block(open->line, open->column);
    if (!block) return oom(p);

    while (!check(p, TOKEN_RBRACE) && !at_end(p) && !p->fatal) {
        int start = p->pos;
        if (!parse_item(p, block)) synchronize(p, start);
    }

    if (!check(p, TOKEN_RBRACE)) {
        if (!p->fatal) {
            error_at(p, open->line, open->column,
                     "Unclosed '{': expected '}' before end of input");
        }
        ast_free(block);
        return NULL;
    }
    advance(p); /* '}' */
    return block;
}

static ASTNode *parse_assignment(Parser *p) {
    const Token *name = advance(p); /* identifier */

    if (check(p, TOKEN_LPAREN)) {
        error_at(p, cur(p)->line, cur(p)->column,
                 "Function calls are not supported in this C subset");
        return NULL;
    }
    if (!check(p, TOKEN_ASSIGN)) {
        char found[64];
        describe(cur(p), found, sizeof found);
        error_at(p, cur(p)->line, cur(p)->column, "Expected '=' after '%.40s', found %s",
                 name->lexeme, found);
        return NULL;
    }
    advance(p); /* '=' */
    if (!require_expr(p, "=")) return NULL;

    ASTNode *value = parse_expression(p);
    if (!value) return NULL;
    if (!expect_semicolon(p, "assignment")) {
        ast_free(value);
        return NULL;
    }
    ASTNode *n = ast_new_assignment(name->lexeme, value, name->line, name->column);
    return n ? n : oom(p);
}

/* int a [= e] (, b [= e])* ;   -> one AST_DECLARATION per name, appended to container */
static int parse_declaration(Parser *p, ASTNode *container) {
    advance(p); /* 'int' */
    do {
        const Token *name = cur(p);
        if (name->type != TOKEN_IDENTIFIER) {
            char found[64];
            describe(name, found, sizeof found);
            error_at(p, name->line, name->column, "Expected a variable name, found %s", found);
            return 0;
        }
        advance(p);
        if (check(p, TOKEN_LPAREN)) {
            error_at(p, cur(p)->line, cur(p)->column,
                     "Functions are not supported in this C subset (only variable declarations)");
            skip_balanced(p, TOKEN_LPAREN, TOKEN_RPAREN); /* skip parameters and body */
            if (check(p, TOKEN_LBRACE)) skip_balanced(p, TOKEN_LBRACE, TOKEN_RBRACE);
            else match(p, TOKEN_SEMICOLON);
            return 0;
        }

        ASTNode *init = NULL;
        if (match(p, TOKEN_ASSIGN)) {
            if (!require_expr(p, "=")) return 0;
            init = parse_expression(p);
            if (!init) return 0;
        }

        ASTNode *decl = ast_new_declaration(name->lexeme, "int", init, name->line, name->column);
        if (!decl) {
            oom(p);
            return 0;
        }
        if (!ast_append_statement(container, decl)) {
            ast_free(decl);
            oom(p);
            return 0;
        }
    } while (match(p, TOKEN_COMMA));

    return expect_semicolon(p, "variable declaration");
}

static ASTNode *parse_statement_inner(Parser *p) {
    const Token *t = cur(p);
    switch (t->type) {
        case TOKEN_IF:         return parse_if(p);
        case TOKEN_WHILE:      return parse_while(p);
        case TOKEN_LBRACE:     return parse_block(p);
        case TOKEN_IDENTIFIER: return parse_assignment(p);
        case TOKEN_INT:
            error_at(p, t->line, t->column,
                     "A declaration is not allowed here; put it inside '{ }'");
            return NULL;
        case TOKEN_ELSE:
            if (p->skip_else) { /* the matching 'if' already failed and was reported */
                p->skip_else = 0;
                advance(p);
                return parse_statement(p);
            }
            error_at(p, t->line, t->column, "'else' without a matching 'if'");
            return NULL;
        case TOKEN_SEMICOLON:
            error_at(p, t->line, t->column,
                     "Empty statement ';' is not supported (use '{ }' for an empty body)");
            return NULL;
        case TOKEN_UNSUPPORTED_KEYWORD:
            report_unsupported(p, t);
            skip_unsupported_statement(p);
            return NULL;
        default: {
            char found[64];
            describe(t, found, sizeof found);
            error_at(p, t->line, t->column, "Expected a statement, found %s", found);
            return NULL;
        }
    }
}

/* A single statement (used for if/while bodies and as the building block of lists). */
static ASTNode *parse_statement(Parser *p) {
    if (!enter(p)) return NULL;
    ASTNode *n = parse_statement_inner(p);
    p->depth--;
    return n;
}

/* One item of a statement list (program or block): a declaration or a statement.
 * Returns 1 on success, 0 on failure. */
static int parse_item(Parser *p, ASTNode *container) {
    if (check(p, TOKEN_INT)) return parse_declaration(p, container);

    ASTNode *s = parse_statement(p);
    if (!s) return 0;
    if (!ast_append_statement(container, s)) {
        ast_free(s);
        oom(p);
        return 0;
    }
    return 1;
}

/* ============================ entry point ============================ */

ASTNode *parser_parse(const TokenArray *tokens, DiagnosticList *diags) {
    if (!tokens || !diags || tokens->count < 1 ||
        tokens->items[tokens->count - 1].type != TOKEN_EOF) {
        if (diags) {
            diag_list_add(diags, DIAG_STAGE_SYNTAX, DIAG_ERROR, 0, 0,
                          "Internal error: token stream does not end with an end-of-input token");
        }
        return NULL;
    }

    Parser p;
    p.tokens = tokens;
    p.pos = 0;
    p.diags = diags;
    p.depth = 0;
    p.errors = 0;
    p.fatal = 0;
    p.last_err_line = 0;
    p.last_err_col = 0;
    p.skip_else = 0;

    ASTNode *program = ast_new_program(1, 1);
    if (!program) {
        oom(&p);
        return NULL;
    }

    while (!at_end(&p) && !p.fatal) {
        if (check(&p, TOKEN_RBRACE)) {
            error_at(&p, cur(&p)->line, cur(&p)->column,
                     "Unexpected '}' with no matching '{'");
            advance(&p);
            continue;
        }
        int start = p.pos;
        if (!parse_item(&p, program)) synchronize(&p, start);
    }

    if (p.errors > 0) {
        ast_free(program);
        return NULL;
    }
    return program;
}
