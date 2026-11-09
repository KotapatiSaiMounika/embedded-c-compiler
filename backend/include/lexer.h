#ifndef LEXER_H
#define LEXER_H

/*
 * lexer.h - lexical analyzer (character-by-character scanner).
 *
 * Usage:
 *     TokenArray tokens;  DiagnosticList diags;
 *     diag_list_init(&diags);
 *     int errors = lexer_tokenize(source, &tokens, &diags);
 *     ...
 *     token_array_free(&tokens);  diag_list_free(&diags);
 */

#include "diagnostics.h"
#include "token.h"

#define LEXER_MAX_SOURCE_LENGTH 65536 /* bytes */

/*
 * Splits `source` into tokens.
 *
 *  - `tokens` is initialised by this function (do not pass a filled array);
 *    the caller must token_array_free() it afterwards.
 *  - The array ALWAYS ends with a TOKEN_EOF token, even when errors occur.
 *  - Every lexical error adds a DIAG_STAGE_LEXICAL diagnostic AND a
 *    TOKEN_INVALID token, then scanning continues (so all errors are found).
 *  - Whitespace and comments are skipped. Columns are 1-based and count
 *    characters (a tab counts as 1).
 *
 * Returns the number of lexical errors (0 = success), or -1 on bad arguments
 * or out-of-memory. The caller must NOT run the parser when the result != 0.
 */
int lexer_tokenize(const char *source, TokenArray *tokens, DiagnosticList *diags);

#endif /* LEXER_H */
