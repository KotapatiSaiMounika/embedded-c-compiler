#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"
#include "test_util.h"

/* Lex + parse `src`. Returns the AST (or NULL); diagnostics go to `diags`. */
static ASTNode *parse_source(const char *src, TokenArray *tokens, DiagnosticList *diags) {
    int lex_errors = lexer_tokenize(src, tokens, diags);
    CHECK_INT(lex_errors, 0);
    return parser_parse(tokens, diags);
}

/* The program must parse and its S-expression must equal `expected`. */
static void expect_ast(const char *src, const char *expected) {
    TokenArray tokens;
    DiagnosticList diags;
    diag_list_init(&diags);
    ASTNode *ast = parse_source(src, &tokens, &diags);
    CHECK(ast != NULL);
    CHECK_INT(diags.count, 0);
    if (ast) {
        StrBuf sb;
        sb_init(&sb);
        ast_write_sexpr(ast, &sb);
        CHECK_STR(sb_cstr(&sb), expected);
        sb_free(&sb);
    } else {
        for (int i = 0; i < diags.count; i++) fprintf(stderr, "    diag: %s\n", diags.items[i].message);
    }
    ast_free(ast);
    token_array_free(&tokens);
    diag_list_free(&diags);
}

/* The program must fail with EXACTLY `n` syntax errors; the first one must contain
 * `msg` and sit at line/col (pass line 0 to skip the position check). */
static void expect_errors(const char *src, int n, const char *msg, int line, int col) {
    TokenArray tokens;
    DiagnosticList diags;
    diag_list_init(&diags);
    ASTNode *ast = parse_source(src, &tokens, &diags);
    CHECK(ast == NULL);
    CHECK_INT(diags.count, n);
    if (diags.count > 0) {
        CHECK_CONTAINS(diags.items[0].message, msg);
        CHECK_INT(diags.items[0].stage, DIAG_STAGE_SYNTAX);
        CHECK_INT(diags.items[0].severity, DIAG_ERROR);
        if (line) {
            CHECK_INT(diags.items[0].line, line);
            CHECK_INT(diags.items[0].column, col);
        }
    }
    if (diags.count != n) {
        for (int i = 0; i < diags.count; i++) {
            fprintf(stderr, "    diag %d (%d:%d): %s\n", i, diags.items[i].line,
                    diags.items[i].column, diags.items[i].message);
        }
    }
    ast_free(ast); /* NULL-safe */
    token_array_free(&tokens);
    diag_list_free(&diags);
}

/* ------------------------------ valid programs ------------------------------ */

static void test_declarations(void) {
    expect_ast("int x;", "(program (decl x))");
    expect_ast("int x = 10;", "(program (decl x 10))");
    expect_ast("int a = 10, b = 20;", "(program (decl a 10) (decl b 20))");
    expect_ast("int a, b = 2, c;", "(program (decl a) (decl b 2) (decl c))");
    expect_ast("int a = 1; int b = a;", "(program (decl a 1) (decl b a))");
    expect_ast("", "(program)");
}

static void test_assignments(void) {
    expect_ast("x = 10;", "(program (= x 10))");
    expect_ast("x = a + b;", "(program (= x (+ a b)))");
    expect_ast("x = x + 1;", "(program (= x (+ x 1)))");
}

static void test_precedence_and_associativity(void) {
    /* the example from the specification */
    expect_ast("int result = 2 + 3 * 4;", "(program (decl result (+ 2 (* 3 4))))");
    expect_ast("x = 2 * 3 + 4;", "(program (= x (+ (* 2 3) 4)))");
    /* left associativity */
    expect_ast("x = 10 - 3 - 2;", "(program (= x (- (- 10 3) 2)))");
    expect_ast("x = 8 / 4 / 2;", "(program (= x (/ (/ 8 4) 2)))");
    expect_ast("x = a - b + c;", "(program (= x (+ (- a b) c)))");
    expect_ast("x = a * b / c;", "(program (= x (/ (* a b) c)))");
    /* relational / equality / logical levels */
    expect_ast("x = a + 1 < b * 2;", "(program (= x (< (+ a 1) (* b 2))))");
    expect_ast("x = a < b == c > d;", "(program (= x (== (< a b) (> c d))))");
    expect_ast("x = a == b != c;", "(program (= x (!= (== a b) c)))");
    expect_ast("x = a <= b >= c;", "(program (= x (>= (<= a b) c)))");
    expect_ast("x = a || b && c;", "(program (= x (|| a (&& b c))))");
    expect_ast("x = a && b || c && d;", "(program (= x (|| (&& a b) (&& c d))))");
    expect_ast("x = a || b || c;", "(program (= x (|| (|| a b) c)))");
    expect_ast("x = a == 1 && b != 2;", "(program (= x (&& (== a 1) (!= b 2))))");
}

static void test_parentheses(void) {
    expect_ast("int x = (2 + 3) * 4;", "(program (decl x (* (+ 2 3) 4)))");
    expect_ast("x = ((1));", "(program (= x 1))");
    expect_ast("x = 2 * (3 + 4) * 5;", "(program (= x (* (* 2 (+ 3 4)) 5)))");
    expect_ast("x = (a || b) && c;", "(program (= x (&& (|| a b) c)))");
}

static void test_unary(void) {
    expect_ast("x = -y;", "(program (= x (neg y)))");
    expect_ast("x = !y;", "(program (= x (not y)))");
    expect_ast("x = - -3;", "(program (= x (neg (neg 3))))");
    expect_ast("x = !!y;", "(program (= x (not (not y))))");
    expect_ast("x = -2 * 3;", "(program (= x (* (neg 2) 3)))");
    expect_ast("x = a - -b;", "(program (= x (- a (neg b))))");
    expect_ast("x = !a && b;", "(program (= x (&& (not a) b)))");
    expect_ast("x = -(a + b);", "(program (= x (neg (+ a b))))");
}

static void test_integer_range(void) {
    expect_ast("x = 2147483647;", "(program (= x 2147483647))");
    expect_ast("x = -2147483648;", "(program (= x -2147483648))"); /* INT_MIN fits */
    expect_ast("x = -2147483647;", "(program (= x (neg 2147483647)))");
    expect_errors("x = 2147483648;", 1, "out of range", 1, 5);
    expect_errors("x = 99999999999999999999999;", 1, "out of range", 1, 5);
    expect_errors("x = 1 - 2147483648;", 1, "out of range", 1, 9);
}

static void test_if_statements(void) {
    expect_ast("if (x > 5) y = 1;", "(program (if (> x 5) (= y 1)))");
    expect_ast("if (x) { y = 1; }", "(program (if x (block (= y 1))))");
    expect_ast("if (x > 5) { y = 1; } else { y = 2; }",
               "(program (if (> x 5) (block (= y 1)) (block (= y 2))))");
    expect_ast("if (a) x = 1; else x = 2;", "(program (if a (= x 1) (= x 2)))");
    /* dangling else binds to the NEAREST if */
    expect_ast("if (a) if (b) x = 1; else x = 2;",
               "(program (if a (if b (= x 1) (= x 2))))");
    /* else-if chains */
    expect_ast("if (a) x = 1; else if (b) x = 2; else x = 3;",
               "(program (if a (= x 1) (if b (= x 2) (= x 3))))");
}

static void test_while_and_blocks(void) {
    expect_ast("while (x < 5) { x = x + 1; }",
               "(program (while (< x 5) (block (= x (+ x 1)))))");
    expect_ast("while (x) x = x - 1;", "(program (while x (= x (- x 1))))");
    expect_ast("{}", "(program (block))");
    expect_ast("{ { int y = 1; } }", "(program (block (block (decl y 1))))");
    expect_ast("{ int a = 1, b = 2; a = b; }",
               "(program (block (decl a 1) (decl b 2) (= a b)))");
    expect_ast("while (a) { while (b) { x = 1; } y = 2; }",
               "(program (while a (block (while b (block (= x 1))) (= y 2))))");
    expect_ast("while (a) { if (b) { if (c) { x = 1; } } }",
               "(program (while a (block (if b (block (if c (block (= x 1))))))))");
}

static void test_comments_inside_programs(void) {
    expect_ast("int x = 1; // trailing\n/* block */ x = 2;", "(program (decl x 1) (= x 2))");
}

static void test_acceptance_program(void) {
    expect_ast("int x = 2;\n\nwhile (x < 5) {\n    x = x + 1;\n}\n",
               "(program (decl x 2) (while (< x 5) (block (= x (+ x 1)))))");
}

static void test_sample_programs(void) {
    expect_ast("int a = 10;\nint b = 20;\nint c = a + b;",
               "(program (decl a 10) (decl b 20) (decl c (+ a b)))");
    expect_ast("int x = 10;\nint y = 0;\nif (x > 5) {\n y = 1;\n} else {\n y = 2;\n}",
               "(program (decl x 10) (decl y 0) (if (> x 5) (block (= y 1)) (block (= y 2))))");
    expect_ast("int x = 10; int y = 20; int result = 0;\n"
               "if (x < y) { if (y > 15) { result = 100; } }",
               "(program (decl x 10) (decl y 20) (decl result 0) "
               "(if (< x y) (block (if (> y 15) (block (= result 100))))))");
    expect_ast("int a = 10; int b = 20; int result = 0;\n"
               "if (a < b && b > 15) { result = 1; }",
               "(program (decl a 10) (decl b 20) (decl result 0) "
               "(if (&& (< a b) (> b 15)) (block (= result 1))))");
}

/* ------------------------------ node details ------------------------------ */

static void test_node_positions_and_fields(void) {
    TokenArray tokens;
    DiagnosticList diags;
    diag_list_init(&diags);
    ASTNode *ast = parse_source("int x = 4 / 2;\nx = -x;", &tokens, &diags);
    CHECK(ast != NULL);
    if (ast) {
        CHECK_INT(ast->type, AST_PROGRAM);
        CHECK_INT(ast->data.program.count, 2);
        ASTNode *decl = ast->data.program.statements[0];
        CHECK_INT(decl->type, AST_DECLARATION);
        CHECK_STR(decl->data.declaration.name, "x");
        CHECK_STR(decl->data.declaration.data_type, "int");
        CHECK_INT(decl->line, 1);
        CHECK_INT(decl->column, 5); /* position of the name */
        ASTNode *div = decl->data.declaration.initializer;
        CHECK_INT(div->type, AST_BINARY_EXPR);
        CHECK_STR(div->data.binary.operator, "/");
        CHECK_INT(div->column, 11); /* position of the operator */
        CHECK_INT(div->data.binary.left->data.integer.value, 4);
        CHECK_INT(div->data.binary.right->data.integer.value, 2);
        ASTNode *asg = ast->data.program.statements[1];
        CHECK_INT(asg->type, AST_ASSIGNMENT);
        CHECK_INT(asg->line, 2);
        CHECK_INT(asg->data.assignment.value->type, AST_UNARY_EXPR);
        CHECK_STR(asg->data.assignment.value->data.unary.operator, "-");
    }
    ast_free(ast);
    token_array_free(&tokens);
    diag_list_free(&diags);
}

typedef struct {
    int nodes;
    int max_depth;
} WalkStats;

static int count_visitor(const ASTNode *n, int depth, void *user) {
    (void)n;
    WalkStats *s = user;
    s->nodes++;
    if (depth > s->max_depth) s->max_depth = depth;
    return 0;
}

static int stop_visitor(const ASTNode *n, int depth, void *user) {
    (void)depth;
    int *seen = user;
    (*seen)++;
    return n->type == AST_BINARY_EXPR; /* stop at the first binary node */
}

static void test_walk_and_print(void) {
    TokenArray tokens;
    DiagnosticList diags;
    diag_list_init(&diags);
    ASTNode *ast = parse_source("int x = 1 + 2;", &tokens, &diags);
    CHECK(ast != NULL);
    if (ast) {
        WalkStats s = {0, 0};
        CHECK_INT(ast_walk(ast, count_visitor, &s), 0);
        CHECK_INT(s.nodes, 5); /* program, decl, +, 1, 2 */
        CHECK_INT(s.max_depth, 3);
        int seen = 0;
        CHECK_INT(ast_walk(ast, stop_visitor, &seen), 1); /* stopped early */
        CHECK_INT(seen, 3);

        FILE *f = tmpfile();
        CHECK(f != NULL);
        if (f) {
            ast_print(ast, f);
            rewind(f);
            char line[128];
            CHECK(fgets(line, sizeof line, f) != NULL);
            CHECK_CONTAINS(line, "Program");
            CHECK(fgets(line, sizeof line, f) != NULL);
            CHECK_CONTAINS(line, "Declaration 'int x'");
            CHECK(fgets(line, sizeof line, f) != NULL);
            CHECK_CONTAINS(line, "initializer: BinaryExpression '+'");
            fclose(f);
        }
    }
    ast_free(ast);
    token_array_free(&tokens);
    diag_list_free(&diags);
}

static void test_ast_json(void) {
    TokenArray tokens;
    DiagnosticList diags;
    diag_list_init(&diags);
    ASTNode *ast = parse_source("int x = 2 + 3;", &tokens, &diags);
    CHECK(ast != NULL);
    char *json = ast_to_json(ast);
    CHECK(json != NULL);
    if (json) {
        CHECK_STR(json,
            "{\"type\":\"Program\",\"label\":\"\",\"line\":1,\"column\":1,\"children\":["
            "{\"role\":\"statement\",\"type\":\"Declaration\",\"label\":\"int x\",\"line\":1,"
            "\"column\":5,\"name\":\"x\",\"dataType\":\"int\",\"children\":["
            "{\"role\":\"initializer\",\"type\":\"BinaryExpression\",\"label\":\"+\",\"line\":1,"
            "\"column\":11,\"operator\":\"+\",\"children\":["
            "{\"role\":\"left\",\"type\":\"IntegerLiteral\",\"label\":\"2\",\"line\":1,\"column\":9,"
            "\"value\":2,\"children\":[]},"
            "{\"role\":\"right\",\"type\":\"IntegerLiteral\",\"label\":\"3\",\"line\":1,\"column\":13,"
            "\"value\":3,\"children\":[]}]}]}]}");
        free(json);
    }
    ast_free(ast);
    token_array_free(&tokens);
    diag_list_free(&diags);

    char *null_json = ast_to_json(NULL);
    CHECK(null_json != NULL);
    if (null_json) {
        CHECK_STR(null_json, "null");
        free(null_json);
    }
}

/* ------------------------------ syntax errors ------------------------------ */

static void test_expression_errors(void) {
    expect_errors("int x = ;", 1, "Expected expression after '='", 1, 9);
    expect_errors("x = ;", 1, "Expected expression after '='", 1, 5);
    expect_errors("x = 5 + ;", 1, "Expected expression after '+'", 1, 9);
    expect_errors("x = 5 * / 2;", 1, "Expected expression after '*'", 1, 9);
    expect_errors("x = a && ;", 1, "Expected expression after '&&'", 1, 10);
    expect_errors("x = -;", 1, "Expected expression after '-'", 1, 6);
    expect_errors("x = !;", 1, "Expected expression after '!'", 1, 6);
    expect_errors("x = ();", 1, "Expected expression after '('", 1, 6);
    expect_errors("x = (1 + 2;", 1, "Expected ')'", 1, 11);
    expect_errors("x = 1 + 2);", 1, "Expected ';' after assignment", 1, 10);
    expect_errors("x = 5 5;", 1, "Expected ';' after assignment", 1, 6);
    expect_errors("x = y = 3;", 1, "Expected ';' after assignment", 1, 6);
}

static void test_missing_semicolons(void) {
    expect_errors("int x = 10", 1, "Expected ';'", 1, 11);
    expect_errors("int x = 10\n", 1, "Expected ';'", 1, 11); /* reported on the line of the last token */
    expect_errors("x = 10", 1, "Expected ';'", 1, 7);
    expect_errors("int x\nint y;", 1, "Expected ';' after variable declaration", 1, 6);
    expect_errors("x = 1\ny = 2;", 1, "Expected ';'", 1, 6);
    expect_errors("int a = 1, b = 2", 1, "Expected ';'", 1, 17);
}

static void test_statement_errors(void) {
    expect_errors("while (x < 5 { x = 1; }", 1, "Expected ')' after condition", 1, 14);
    expect_errors("while x < 5 { }", 1, "Expected '(' after 'while'", 1, 7);
    expect_errors("if x > 5 { }", 1, "Expected '(' after 'if'", 1, 4);
    expect_errors("if (x = 5) { }", 1, "use '=='", 1, 7);
    expect_errors("if () { }", 1, "Expected expression after '('", 1, 5);
    expect_errors("x + 1;", 1, "Expected '=' after 'x'", 1, 3);
    expect_errors("else x = 1;", 1, "'else' without a matching 'if'", 1, 1);
    expect_errors("if (a) x = 1; else", 1, "Expected a statement, found end of input", 1, 19);
    expect_errors("int ;", 1, "Expected a variable name", 1, 5);
    expect_errors("int a, ;", 1, "Expected a variable name", 1, 8);
    expect_errors("int = 5;", 1, "Expected a variable name", 1, 5);
    expect_errors("int 5;", 1, "Expected a variable name", 1, 5);
}

static void test_blocks_and_braces(void) {
    expect_errors("{ int x = 1;", 1, "Unclosed '{'", 1, 1);
    expect_errors("while (a) { if (b) { x = 1; }", 1, "Unclosed '{'", 1, 11);
    expect_errors("}", 1, "Unexpected '}'", 1, 1);
    expect_errors("int x; }", 1, "Unexpected '}'", 1, 8);
    expect_errors("if (a) }", 1, "Expected a statement, found '}'", 1, 8);
}

static void test_scope_restrictions(void) {
    expect_errors("if (a) int y;", 1, "declaration is not allowed here", 1, 8);
    expect_errors("while (a) int y = 1;", 1, "declaration is not allowed here", 1, 11);
    expect_errors(";", 1, "Empty statement", 1, 1);
    expect_errors("while (a);", 1, "Empty statement", 1, 10);
}

static void test_unsupported_constructs(void) {
    expect_errors("for (int i = 0; i < 5; i = i + 1) { x = 1; }", 1, "'for' loops are not supported", 1, 1);
    expect_errors("break;", 1, "'break' is not supported", 1, 1);
    expect_errors("continue;", 1, "'continue' is not supported", 1, 1);
    expect_errors("return 0;", 1, "'return' is not supported", 1, 1);
    expect_errors("float f = 1;", 1, "Type 'float' is not supported", 1, 1);
    expect_errors("switch (x) { break; }", 1, "'switch' is not supported", 1, 1);
    expect_errors("int main() { return 0; }", 1, "Functions are not supported", 1, 9);
    expect_errors("foo(1);", 1, "Function calls are not supported", 1, 4);
    expect_errors("x = sizeof(y);", 1, "'sizeof' is not supported", 1, 5);
}

static void test_error_recovery_reports_multiple_errors(void) {
    /* three independent mistakes -> three diagnostics, no extra cascade errors */
    expect_errors("int x = ;\nint y = ;\nint z = 1\nint w = 2;", 3, "Expected expression after '='", 1, 9);
    expect_errors("x = ;\ny = 1;\nz = ;", 2, "Expected expression after '='", 1, 5);
    /* errors inside a block do not abort the block */
    expect_errors("{ x = ; y = 1; z = ; }", 2, "Expected expression after '='", 1, 7);
    /* an error in the condition does not make the body produce more errors */
    expect_errors("if (x < ) { y = 1; } else { y = 2; }", 1, "Expected expression after '<'", 1, 9);
    /* unsupported construct is skipped as a whole */
    expect_errors("for (int i = 0; i < 3; i = i + 1) { x = 1; }\nint y = 1;", 1, "'for'", 1, 1);
    /* the slide's typo: '{ ... ;)' */
    expect_errors("int x = 2; while (x < 5) { x = x + 1; )", 2, "Expected a statement, found ')'", 1, 39);
}

static void test_no_infinite_loops_on_garbage(void) {
    /* must terminate and fail; we only care that these return */
    const char *garbage[] = {")", "))))", "( ( (", "= = =", "int int int", "if if if", "{ { {",
                             "} } }", "else else", "x y z", "1 2 3", "&& ||", ", , ,", "int x = = = ;"};
    for (size_t i = 0; i < sizeof garbage / sizeof garbage[0]; i++) {
        TokenArray tokens;
        DiagnosticList diags;
        diag_list_init(&diags);
        ASTNode *ast = parse_source(garbage[i], &tokens, &diags);
        CHECK(ast == NULL);
        CHECK(diags.count >= 1);
        ast_free(ast);
        token_array_free(&tokens);
        diag_list_free(&diags);
    }
}

/* ------------------------------ limits / robustness ------------------------------ */

static void test_deep_nesting_is_rejected_not_crashed(void) {
    /* 1000 nested parentheses */
    size_t n = 1000;
    char *src = malloc(n * 2 + 8);
    CHECK(src != NULL);
    if (!src) return;
    char *w = src;
    memcpy(w, "x = ", 4); w += 4;
    for (size_t i = 0; i < n; i++) *w++ = '(';
    *w++ = '1';
    for (size_t i = 0; i < n; i++) *w++ = ')';
    *w++ = ';';
    *w = '\0';
    expect_errors(src, 1, "nested too deeply", 0, 0);
    free(src);

    /* 1000 nested blocks */
    char *blocks = malloc(n * 2 + 1);
    CHECK(blocks != NULL);
    if (!blocks) return;
    memset(blocks, '{', n);
    memset(blocks + n, '}', n);
    blocks[2 * n] = '\0';
    expect_errors(blocks, 1, "nested too deeply", 0, 0);
    free(blocks);

    /* a long chain of unary operators */
    char *nots = malloc(1000 + 16);
    CHECK(nots != NULL);
    if (!nots) return;
    strcpy(nots, "x = ");
    memset(nots + 4, '!', 900);
    strcpy(nots + 904, "1;");
    expect_errors(nots, 1, "nested too deeply", 0, 0);
    free(nots);
}

static void test_long_flat_programs_are_fine(void) {
    /* 5000 statements, and nesting just under the limit */
    size_t cap = 5000 * 8 + 16;
    char *src = malloc(cap);
    CHECK(src != NULL);
    if (!src) return;
    src[0] = '\0';
    for (int i = 0; i < 5000; i++) strcat(src, "x = 1;\n");
    TokenArray tokens;
    DiagnosticList diags;
    diag_list_init(&diags);
    ASTNode *ast = parse_source(src, &tokens, &diags);
    CHECK(ast != NULL);
    if (ast) CHECK_INT(ast->data.program.count, 5000);
    ast_free(ast);
    token_array_free(&tokens);
    diag_list_free(&diags);
    free(src);

    char *nested = malloc(512);
    CHECK(nested != NULL);
    if (!nested) return;
    memset(nested, '{', 100);
    memset(nested + 100, '}', 100);
    nested[200] = '\0';
    TokenArray t2;
    DiagnosticList d2;
    diag_list_init(&d2);
    ASTNode *a2 = parse_source(nested, &t2, &d2);
    CHECK(a2 != NULL);
    ast_free(a2);
    token_array_free(&t2);
    diag_list_free(&d2);
    free(nested);
}

static void test_very_long_expression_chain(void) {
    /* 5000 operators in one chain would build a 5000-deep left tree: rejected cleanly */
    size_t n = 5000;
    char *src = malloc(n * 4 + 16);
    CHECK(src != NULL);
    if (!src) return;
    strcpy(src, "x = 1");
    for (size_t i = 0; i < n; i++) strcat(src, " + 1");
    strcat(src, ";");
    expect_errors(src, 1, "too long or too deeply nested", 0, 0);
    free(src);

    /* 500 operators is fine */
    char *ok = malloc(500 * 4 + 16);
    CHECK(ok != NULL);
    if (!ok) return;
    strcpy(ok, "x = 1");
    for (int i = 0; i < 500; i++) strcat(ok, " + 1");
    strcat(ok, ";");
    TokenArray tokens;
    DiagnosticList diags;
    diag_list_init(&diags);
    ASTNode *ast = parse_source(ok, &tokens, &diags);
    CHECK(ast != NULL);
    char *json = ast_to_json(ast);
    CHECK(json != NULL);
    free(json);
    ast_free(ast);
    token_array_free(&tokens);
    diag_list_free(&diags);
    free(ok);
}

static void test_bad_arguments(void) {
    DiagnosticList diags;
    diag_list_init(&diags);
    CHECK(parser_parse(NULL, &diags) == NULL);
    CHECK_INT(diags.count, 1);
    TokenArray empty;
    token_array_init(&empty);
    CHECK(parser_parse(&empty, &diags) == NULL);
    CHECK_INT(diags.count, 2);
    diag_list_free(&diags);
}

static void test_diagnostic_cap(void) {
    /* 300 bad statements: the list stops growing at DIAG_MAX_COUNT */
    char *src = malloc(300 * 8 + 16);
    CHECK(src != NULL);
    if (!src) return;
    src[0] = '\0';
    for (int i = 0; i < 300; i++) strcat(src, "x = ;\n");
    TokenArray tokens;
    DiagnosticList diags;
    diag_list_init(&diags);
    ASTNode *ast = parse_source(src, &tokens, &diags);
    CHECK(ast == NULL);
    CHECK_INT(diags.count, DIAG_MAX_COUNT);
    CHECK_CONTAINS(diags.items[DIAG_MAX_COUNT - 1].message, "Too many diagnostics");
    token_array_free(&tokens);
    diag_list_free(&diags);
    free(src);
}

int main(void) {
    RUN_TEST(test_declarations);
    RUN_TEST(test_assignments);
    RUN_TEST(test_precedence_and_associativity);
    RUN_TEST(test_parentheses);
    RUN_TEST(test_unary);
    RUN_TEST(test_integer_range);
    RUN_TEST(test_if_statements);
    RUN_TEST(test_while_and_blocks);
    RUN_TEST(test_comments_inside_programs);
    RUN_TEST(test_acceptance_program);
    RUN_TEST(test_sample_programs);
    RUN_TEST(test_node_positions_and_fields);
    RUN_TEST(test_walk_and_print);
    RUN_TEST(test_ast_json);
    RUN_TEST(test_expression_errors);
    RUN_TEST(test_missing_semicolons);
    RUN_TEST(test_statement_errors);
    RUN_TEST(test_blocks_and_braces);
    RUN_TEST(test_scope_restrictions);
    RUN_TEST(test_unsupported_constructs);
    RUN_TEST(test_error_recovery_reports_multiple_errors);
    RUN_TEST(test_no_infinite_loops_on_garbage);
    RUN_TEST(test_deep_nesting_is_rejected_not_crashed);
    RUN_TEST(test_long_flat_programs_are_fine);
    RUN_TEST(test_very_long_expression_chain);
    RUN_TEST(test_bad_arguments);
    RUN_TEST(test_diagnostic_cap);
    return TEST_SUMMARY();
}
