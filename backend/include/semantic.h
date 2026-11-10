#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"
#include "diagnostics.h"

int semantic_analyze(const ASTNode *root, DiagnosticList *diags);

#endif