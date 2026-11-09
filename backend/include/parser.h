#ifndef PARSER_H
#define PARSER_H

/*
 * parser.h - recursive descent parser (tokens -> AST).
 *
 * Grammar: see docs/grammar.md
 */

#include "ast.h"
#include "diagnostics.h"
#include "token.h"

#define PARSER_MAX_DEPTH 256 /* nesting limit for statements / parentheses / unary chains */

/*
 * Parses a token stream produced by lexer_tokenize() (it must end with
 * TOKEN_EOF).
 *
 * Returns the AST root (an AST_PROGRAM node, owned by the caller -> ast_free)
 * when the program is syntactically valid.
 *
 * Returns NULL if there was ANY syntax error. In that case every error has
 * been added to `diags` (stage DIAG_STAGE_SYNTAX) - the parser recovers after
 * each error so several mistakes are reported in one run - and no partial AST
 * is returned, so invalid programs can never reach later stages.
 *
 * `tokens` is only read, never modified or freed.
 */
ASTNode *parser_parse(const TokenArray *tokens, DiagnosticList *diags);

#endif /* PARSER_H */
