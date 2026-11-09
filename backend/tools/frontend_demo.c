/*
 * frontend_demo - run the lexer and parser on a file (or stdin) and show the result.
 *
 *   ./frontend_demo program.c            tokens + AST tree
 *   ./frontend_demo --json program.c     tokens, AST and diagnostics as JSON
 *   echo "int x = 5;" | ./frontend_demo -
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"

static char *read_all(FILE *f) {
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += n;
        if (len + 1 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) {
                free(buf);
                return NULL;
            }
            buf = nb;
        }
    }
    buf[len] = '\0';
    return buf;
}

int main(int argc, char **argv) {
    int json = 0;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) json = 1;
        else path = argv[i];
    }
    if (!path) {
        fprintf(stderr, "usage: %s [--json] <file.c | ->\n", argv[0]);
        return 2;
    }

    FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
    if (!f) {
        perror(path);
        return 2;
    }
    char *source = read_all(f);
    if (f != stdin) fclose(f);
    if (!source) {
        fprintf(stderr, "out of memory\n");
        return 2;
    }

    TokenArray tokens;
    DiagnosticList diags;
    diag_list_init(&diags);

    int lex_errors = lexer_tokenize(source, &tokens, &diags);
    ASTNode *ast = NULL;
    if (lex_errors == 0) ast = parser_parse(&tokens, &diags); /* only parse clean token streams */

    if (json) {
        char *tj = token_array_to_json(&tokens);
        char *aj = ast_to_json(ast);
        char *dj = diag_list_to_json(&diags);
        printf("{\"success\":%s,\n\"tokens\":%s,\n\"ast\":%s,\n\"diagnostics\":%s}\n",
               ast ? "true" : "false", tj ? tj : "[]", aj ? aj : "null", dj ? dj : "[]");
        free(tj);
        free(aj);
        free(dj);
    } else {
        puts("=== TOKENS ===");
        token_array_print(&tokens, stdout);
        if (ast) {
            puts("\n=== AST ===");
            ast_print(ast, stdout);
        }
        if (diags.count > 0) {
            puts("\n=== DIAGNOSTICS ===");
            for (int i = 0; i < diags.count; i++) {
                const Diagnostic *d = &diags.items[i];
                printf("%s %s at line %d, column %d: %s\n", diag_stage_name(d->stage),
                       diag_severity_name(d->severity), d->line, d->column, d->message);
            }
        }
    }

    int failed = (ast == NULL);
    ast_free(ast);
    token_array_free(&tokens);
    diag_list_free(&diags);
    free(source);
    return failed ? 1 : 0;
}
