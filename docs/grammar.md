# Supported grammar (lexer + parser)

## Tokens

| Group | Tokens |
|---|---|
| Keywords | `int` `if` `else` `while` |
| Reserved but unsupported (reported by the parser) | `for do switch case default break continue return goto float double char short long unsigned signed void struct union enum typedef sizeof const static extern volatile register auto` |
| Literals / names | `IDENTIFIER` = `[A-Za-z_][A-Za-z0-9_]*`, `INTEGER_LITERAL` = decimal digits, no leading zeros |
| Operators | `+ - * / = == != < > <= >= && \|\| !` |
| Delimiters | `; , ( ) { }` |
| Skipped | whitespace, `// ...`, `/* ... */` |

Lexical errors (each gives a diagnostic and an `INVALID` token, scanning continues):
invalid characters (`@ $ ` ~ ...`), lone `&` / `|`, `%`, `[ ]`, `++ -- += -= *= /=`,
`#...` preprocessor lines, string/char literals, malformed numbers (`12abc`, `0x1F`, `007`),
non-ASCII characters, unterminated `/* ...`, source larger than 65536 bytes.

## Grammar (recursive descent, one function per rule)

```text
program        -> item*
item           -> declaration | statement
statement      -> assignment | ifStatement | whileStatement | block

declaration    -> "int" declarator ("," declarator)* ";"
declarator     -> IDENTIFIER ("=" expression)?
assignment     -> IDENTIFIER "=" expression ";"
ifStatement    -> "if" "(" expression ")" statement ("else" statement)?
whileStatement -> "while" "(" expression ")" statement
block          -> "{" item* "}"

expression     -> logicalOr
logicalOr      -> logicalAnd ( "||" logicalAnd )*
logicalAnd     -> equality   ( "&&" equality )*
equality       -> relational ( ("==" | "!=") relational )*
relational     -> additive   ( ("<" | ">" | "<=" | ">=") additive )*
additive       -> multiplicative ( ("+" | "-") multiplicative )*
multiplicative -> unary ( ("*" | "/") unary )*
unary          -> ("-" | "!") unary | primary
primary        -> INTEGER_LITERAL | IDENTIFIER | "(" expression ")"
```

All binary operators are **left associative**. The dangling `else` binds to the nearest `if`.

## Deliberate decisions (differences from the plain spec grammar)

1. **Declarations are not allowed as the direct body of `if` / `while`.**
   `if (x) int y;` is an error ("put it inside `{ }`"), exactly like C. A declaration inside `{ }` is fine.
2. **`int a = 1, b = 2;` becomes two `Declaration` AST nodes**, one per variable.
3. **Integer literals are range-checked by the parser.** A literal larger than 2147483647 is a syntax-stage error,
   except `-2147483648`, which the parser folds into a single `IntegerLiteral(-2147483648)` node.
   So every `IntegerLiteral.value` that reaches the semantic analyzer already fits in `int32_t`.
4. **Empty statements (`;`) are rejected.** Use `{ }` for an empty body.
5. **Nesting limits:** statements/parentheses/unary chains 256 levels; expression tree height 1000.
6. **Parentheses do not create AST nodes.**
7. Binary nodes carry the line/column of the **operator**; declarations/assignments carry the line/column of the **variable name**.
   (Handy for "Division by zero" and "undeclared variable" messages.)

## Error recovery

After a syntax error the parser skips to the next `;` or statement start and continues,
so several mistakes are reported in one run. If there is any error, `parser_parse` returns `NULL`
(no partial AST). At most one error is reported per source position.

---

# Interface contract for the other modules

```c
#include "lexer.h"
#include "parser.h"

DiagnosticList diags;  diag_list_init(&diags);
TokenArray tokens;

int lex_errors = lexer_tokenize(source, &tokens, &diags);   // tokens always ends with EOF
ASTNode *ast = NULL;
if (lex_errors == 0) ast = parser_parse(&tokens, &diags);   // NULL on any syntax error

// ... Person 2: semantic_analyze(ast, &diags) etc.

char *tokens_json = token_array_to_json(&tokens);   // caller free()s
char *ast_json    = ast_to_json(ast);               // "null" if ast == NULL
char *diags_json  = diag_list_to_json(&diags);

ast_free(ast);  token_array_free(&tokens);  diag_list_free(&diags);
```

* `lexer_tokenize` returns the number of lexical errors (-1 = out of memory / bad arguments). **Do not parse if it is non-zero.**
* `diag_list_add(&diags, DIAG_STAGE_SEMANTIC, DIAG_ERROR, line, col, "Variable '%s' is not declared", name);` - same call for every stage.
* In `server.c` the JSON strings can be embedded without re-parsing:
  `cJSON_AddRawToObject(root, "tokens", tokens_json);` or sent directly with `mg_http_reply`.
* `ast_walk`, `ast_child_count`, `ast_child_at` give a generic traversal; node fields follow the specification (`data.binary.operator`, `data.declaration.name`, ...).
