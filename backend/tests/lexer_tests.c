#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "test_util.h"

/* Lexes `src`, fills `out` with a space separated list of token type names. */
static int lex_types(const char *src, char *out, size_t size, DiagnosticList *diags) {
    TokenArray tokens;
    int errors = lexer_tokenize(src, &tokens, diags);
    out[0] = '\0';
    for (int i = 0; i < tokens.count; i++) {
        if (i > 0) strncat(out, " ", size - strlen(out) - 1);
        strncat(out, token_type_name(tokens.items[i].type), size - strlen(out) - 1);
    }
    token_array_free(&tokens);
    return errors;
}

/* Convenience: lex and compare type list, expecting no errors. */
static void expect_types(const char *src, const char *expected) {
    char buf[1024];
    DiagnosticList d;
    diag_list_init(&d);
    int errors = lex_types(src, buf, sizeof buf, &d);
    CHECK_INT(errors, 0);
    CHECK_STR(buf, expected);
    diag_list_free(&d);
}

static void test_spec_example(void) {
    expect_types("int x = 10;",
                 "KEYWORD_INT IDENTIFIER ASSIGN INTEGER_LITERAL SEMICOLON EOF");
}

static void test_keywords(void) {
    expect_types("int if else while", "KEYWORD_INT KEYWORD_IF KEYWORD_ELSE KEYWORD_WHILE EOF");
    /* keywords are case sensitive */
    expect_types("Int IF While", "IDENTIFIER IDENTIFIER IDENTIFIER EOF");
    /* unsupported C keywords get their own token type */
    expect_types("for break return float", "UNSUPPORTED_KEYWORD UNSUPPORTED_KEYWORD "
                                           "UNSUPPORTED_KEYWORD UNSUPPORTED_KEYWORD EOF");
}

static void test_identifiers(void) {
    TokenArray t;
    DiagnosticList d;
    diag_list_init(&d);
    CHECK_INT(lexer_tokenize("abc _x y2 integer iff while1 _", &t, &d), 0);
    CHECK_INT(t.count, 8);
    for (int i = 0; i < 7; i++) CHECK_INT(t.items[i].type, TOKEN_IDENTIFIER);
    CHECK_STR(t.items[0].lexeme, "abc");
    CHECK_STR(t.items[1].lexeme, "_x");
    CHECK_STR(t.items[3].lexeme, "integer"); /* starts with a keyword, still an identifier */
    CHECK_STR(t.items[4].lexeme, "iff");
    CHECK_STR(t.items[5].lexeme, "while1");
    token_array_free(&t);
    diag_list_free(&d);
}

static void test_integer_literals(void) {
    TokenArray t;
    DiagnosticList d;
    diag_list_init(&d);
    CHECK_INT(lexer_tokenize("0 7 42 2147483647 99999999999999999999", &t, &d), 0);
    CHECK_INT(t.count, 6);
    CHECK_STR(t.items[0].lexeme, "0");
    CHECK_STR(t.items[3].lexeme, "2147483647");
    CHECK_INT(t.items[4].type, TOKEN_INTEGER_LITERAL); /* range is checked by the parser */
    token_array_free(&t);
    diag_list_free(&d);

    /* malformed numbers */
    const char *bad[] = {"12abc", "0x1F", "007", "3_000"};
    for (int i = 0; i < 4; i++) {
        DiagnosticList d2;
        diag_list_init(&d2);
        char buf[256];
        CHECK_INT(lex_types(bad[i], buf, sizeof buf, &d2), 1);
        CHECK_STR(buf, "INVALID EOF");
        diag_list_free(&d2);
    }
}

static void test_arithmetic_operators(void) {
    expect_types("+ - * /", "PLUS MINUS MULTIPLY DIVIDE EOF");
    expect_types("a+b-c*d/e",
                 "IDENTIFIER PLUS IDENTIFIER MINUS IDENTIFIER MULTIPLY IDENTIFIER DIVIDE "
                 "IDENTIFIER EOF");
}

static void test_relational_and_assignment_operators(void) {
    expect_types("< > <= >= == != =", "LESS GREATER LESS_EQUAL GREATER_EQUAL EQUAL NOT_EQUAL ASSIGN EOF");
    /* longest match wins, no spaces needed */
    expect_types("a<=b", "IDENTIFIER LESS_EQUAL IDENTIFIER EOF");
    expect_types("a==b", "IDENTIFIER EQUAL IDENTIFIER EOF");
    expect_types("a=!b", "IDENTIFIER ASSIGN NOT IDENTIFIER EOF");
    expect_types("a!=b", "IDENTIFIER NOT_EQUAL IDENTIFIER EOF");
    expect_types("===", "EQUAL ASSIGN EOF");
}

static void test_logical_operators(void) {
    expect_types("&& || !", "AND OR NOT EOF");
    expect_types("a&&b||!c", "IDENTIFIER AND IDENTIFIER OR NOT IDENTIFIER EOF");
}

static void test_delimiters(void) {
    expect_types("; , ( ) { }", "SEMICOLON COMMA LPAREN RPAREN LBRACE RBRACE EOF");
}

static void test_comments_and_whitespace(void) {
    expect_types("int // comment int\nx", "KEYWORD_INT IDENTIFIER EOF");
    expect_types("int /* a \n multi-line \n comment */ x", "KEYWORD_INT IDENTIFIER EOF");
    expect_types("a/**/b", "IDENTIFIER IDENTIFIER EOF");
    expect_types("a / b", "IDENTIFIER DIVIDE IDENTIFIER EOF");
    expect_types("// only a comment", "EOF");
    expect_types("   \t\r\n  \n ", "EOF");
    expect_types("", "EOF");
    expect_types("/* // */ x // /* \n y", "IDENTIFIER IDENTIFIER EOF");
    /* non-ASCII text is fine inside comments */
    expect_types("// caf\xC3\xA9\nx", "IDENTIFIER EOF");

    TokenArray t;
    DiagnosticList d;
    diag_list_init(&d);
    CHECK_INT(lexer_tokenize("x /* never closed", &t, &d), 1);
    CHECK_INT(d.count, 1);
    CHECK_CONTAINS(d.items[0].message, "Unterminated comment");
    CHECK_INT(d.items[0].line, 1);
    CHECK_INT(d.items[0].column, 3);
    CHECK_INT(t.items[t.count - 1].type, TOKEN_EOF);
    token_array_free(&t);
    diag_list_free(&d);

    /* the text slash-star-slash opens a comment but does not close it */
    DiagnosticList d2;
    diag_list_init(&d2);
    char buf[64];
    CHECK_INT(lex_types("/*/", buf, sizeof buf, &d2), 1);
    diag_list_free(&d2);
}

static void test_positions(void) {
    TokenArray t;
    DiagnosticList d;
    diag_list_init(&d);
    const char *src = "int x = 5;\n  while (x)\n\ty;";
    CHECK_INT(lexer_tokenize(src, &t, &d), 0);

    CHECK_INT(t.items[0].line, 1); CHECK_INT(t.items[0].column, 1);  /* int   */
    CHECK_INT(t.items[1].line, 1); CHECK_INT(t.items[1].column, 5);  /* x     */
    CHECK_INT(t.items[2].line, 1); CHECK_INT(t.items[2].column, 7);  /* =     */
    CHECK_INT(t.items[3].line, 1); CHECK_INT(t.items[3].column, 9);  /* 5     */
    CHECK_INT(t.items[4].line, 1); CHECK_INT(t.items[4].column, 10); /* ;     */
    CHECK_STR(t.items[5].lexeme, "while");
    CHECK_INT(t.items[5].line, 2); CHECK_INT(t.items[5].column, 3);
    CHECK_INT(t.items[6].line, 2); CHECK_INT(t.items[6].column, 9);  /* (     */
    CHECK_STR(t.items[8].lexeme, ")");
    CHECK_INT(t.items[9].line, 3); CHECK_INT(t.items[9].column, 2);  /* y, after a tab */
    CHECK_INT(t.items[11].type, TOKEN_EOF);
    CHECK_INT(t.items[11].line, 3);

    token_array_free(&t);
    diag_list_free(&d);

    /* positions after multi-line comment and CRLF */
    TokenArray t2;
    DiagnosticList d2;
    diag_list_init(&d2);
    CHECK_INT(lexer_tokenize("/* a\nb */ x\r\ny", &t2, &d2), 0);
    CHECK_INT(t2.items[0].line, 2); CHECK_INT(t2.items[0].column, 6);
    CHECK_INT(t2.items[1].line, 3); CHECK_INT(t2.items[1].column, 1);
    token_array_free(&t2);
    diag_list_free(&d2);
}

static void test_invalid_characters(void) {
    TokenArray t;
    DiagnosticList d;
    diag_list_init(&d);
    CHECK_INT(lexer_tokenize("int x = 10 @ 2;", &t, &d), 1);
    CHECK_INT(d.count, 1);
    CHECK_STR(d.items[0].message, "Invalid character '@'");
    CHECK_INT(d.items[0].stage, DIAG_STAGE_LEXICAL);
    CHECK_INT(d.items[0].severity, DIAG_ERROR);
    CHECK_INT(d.items[0].line, 1);
    CHECK_INT(d.items[0].column, 12);
    /* scanning continues after the error: the rest is still tokenized */
    CHECK_INT(t.items[4].type, TOKEN_INVALID);
    CHECK_STR(t.items[4].lexeme, "@");
    CHECK_INT(t.items[5].type, TOKEN_INTEGER_LITERAL);
    CHECK_INT(t.items[t.count - 1].type, TOKEN_EOF);
    token_array_free(&t);
    diag_list_free(&d);

    /* several errors are all reported */
    DiagnosticList d2;
    diag_list_init(&d2);
    char buf[256];
    CHECK_INT(lex_types("$ x ` y ~", buf, sizeof buf, &d2), 3);
    CHECK_INT(d2.count, 3);
    diag_list_free(&d2);

    /* lone & and | are not operators here */
    DiagnosticList d3;
    diag_list_init(&d3);
    CHECK_INT(lex_types("a & b | c", buf, sizeof buf, &d3), 2);
    CHECK_CONTAINS(d3.items[0].message, "did you mean '&&'");
    CHECK_CONTAINS(d3.items[1].message, "did you mean '||'");
    diag_list_free(&d3);

    /* unsupported punctuation */
    DiagnosticList d4;
    diag_list_init(&d4);
    CHECK_INT(lex_types("a % b [ ]", buf, sizeof buf, &d4), 3);
    diag_list_free(&d4);
}

static void test_unsupported_operators_and_literals(void) {
    char buf[256];
    DiagnosticList d;
    diag_list_init(&d);
    CHECK_INT(lex_types("x++", buf, sizeof buf, &d), 1);
    CHECK_CONTAINS(d.items[0].message, "Operator '++' is not supported");
    diag_list_free(&d);

    DiagnosticList d2;
    diag_list_init(&d2);
    CHECK_INT(lex_types("x += 1; y -= 2; z--;", buf, sizeof buf, &d2), 3);
    diag_list_free(&d2);

    DiagnosticList d3;
    diag_list_init(&d3);
    CHECK_INT(lex_types("#include <stdio.h>\nint x;", buf, sizeof buf, &d3), 1);
    CHECK_CONTAINS(d3.items[0].message, "Preprocessor");
    CHECK_STR(buf, "INVALID KEYWORD_INT IDENTIFIER SEMICOLON EOF");
    diag_list_free(&d3);

    DiagnosticList d4;
    diag_list_init(&d4);
    CHECK_INT(lex_types("x = \"hello world\"; y = 'a';", buf, sizeof buf, &d4), 2);
    diag_list_free(&d4);
}

static void test_non_ascii_and_control_chars(void) {
    TokenArray t;
    DiagnosticList d;
    diag_list_init(&d);
    /* a UTF-8 "smart quote" is ONE invalid token, columns count characters */
    CHECK_INT(lexer_tokenize("x \xE2\x80\x9C y", &t, &d), 1);
    CHECK_INT(t.count, 4);
    CHECK_INT(t.items[1].type, TOKEN_INVALID);
    CHECK_INT(t.items[2].column, 5);
    CHECK_CONTAINS(d.items[0].message, "non-ASCII");
    token_array_free(&t);
    diag_list_free(&d);

    DiagnosticList d2;
    diag_list_init(&d2);
    char buf[128];
    CHECK_INT(lex_types("x \x01 y", buf, sizeof buf, &d2), 1);
    CHECK_CONTAINS(d2.items[0].message, "control character");
    diag_list_free(&d2);
}

static void test_limits_and_bad_arguments(void) {
    TokenArray t;
    DiagnosticList d;
    diag_list_init(&d);

    CHECK_INT(lexer_tokenize(NULL, &t, &d), 0); /* NULL source behaves like "" */
    CHECK_INT(t.count, 1);
    token_array_free(&t);

    CHECK_INT(lexer_tokenize("x", NULL, &d), -1);
    CHECK_INT(lexer_tokenize("x", &t, NULL), -1);

    size_t n = LEXER_MAX_SOURCE_LENGTH + 10;
    char *big = malloc(n + 1);
    CHECK(big != NULL);
    if (big) {
        memset(big, 'a', n);
        big[n] = '\0';
        CHECK_INT(lexer_tokenize(big, &t, &d), 1);
        CHECK_CONTAINS(d.items[d.count - 1].message, "too large");
        CHECK_INT(t.items[t.count - 1].type, TOKEN_EOF);
        token_array_free(&t);
        free(big);
    }
    diag_list_free(&d);
}

static void test_json_output(void) {
    TokenArray t;
    DiagnosticList d;
    diag_list_init(&d);
    CHECK_INT(lexer_tokenize("int x = 10;", &t, &d), 0);
    char *json = token_array_to_json(&t);
    CHECK(json != NULL);
    if (json) {
        CHECK_CONTAINS(json, "{\"lexeme\":\"int\",\"type\":\"KEYWORD_INT\",\"category\":\"keyword\","
                             "\"line\":1,\"column\":1}");
        CHECK_CONTAINS(json, "\"type\":\"INTEGER_LITERAL\"");
        CHECK_CONTAINS(json, "\"type\":\"EOF\"");
        free(json);
    }
    token_array_free(&t);
    diag_list_free(&d);

    /* diagnostics JSON escapes quotes safely */
    DiagnosticList d2;
    diag_list_init(&d2);
    diag_list_add(&d2, DIAG_STAGE_LEXICAL, DIAG_ERROR, 3, 4, "bad \"quote\" \\ and\nnewline");
    char *dj = diag_list_to_json(&d2);
    CHECK(dj != NULL);
    if (dj) {
        CHECK_STR(dj, "[{\"stage\":\"lexical\",\"severity\":\"error\","
                      "\"message\":\"bad \\\"quote\\\" \\\\ and\\nnewline\",\"line\":3,\"column\":4}]");
        free(dj);
    }
    diag_list_free(&d2);
}

static void test_default_program(void) {
    expect_types("int x = 2;\n\nwhile (x < 5) {\n    x = x + 1;\n}\n",
                 "KEYWORD_INT IDENTIFIER ASSIGN INTEGER_LITERAL SEMICOLON "
                 "KEYWORD_WHILE LPAREN IDENTIFIER LESS INTEGER_LITERAL RPAREN LBRACE "
                 "IDENTIFIER ASSIGN IDENTIFIER PLUS INTEGER_LITERAL SEMICOLON RBRACE EOF");
}

int main(void) {
    RUN_TEST(test_spec_example);
    RUN_TEST(test_keywords);
    RUN_TEST(test_identifiers);
    RUN_TEST(test_integer_literals);
    RUN_TEST(test_arithmetic_operators);
    RUN_TEST(test_relational_and_assignment_operators);
    RUN_TEST(test_logical_operators);
    RUN_TEST(test_delimiters);
    RUN_TEST(test_comments_and_whitespace);
    RUN_TEST(test_positions);
    RUN_TEST(test_invalid_characters);
    RUN_TEST(test_unsupported_operators_and_literals);
    RUN_TEST(test_non_ascii_and_control_chars);
    RUN_TEST(test_limits_and_bad_arguments);
    RUN_TEST(test_json_output);
    RUN_TEST(test_default_program);
    return TEST_SUMMARY();
}
