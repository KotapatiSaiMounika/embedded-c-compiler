#ifndef IR_H
#define IR_H

#include "ast.h"
#include "diagnostics.h"

typedef struct {
    char *op;
    char *arg1;
    char *arg2;
    char *result;
    int line;
} IRInstruction;

typedef struct {
    IRInstruction *instructions;
    int count;
    int capacity;
    int temp_counter;
    int label_counter;
} IRProgram;

void ir_init(IRProgram *ir);
void ir_free(IRProgram *ir);
int ir_generate(const ASTNode *root, IRProgram *ir, DiagnosticList *diags);
void ir_print(const IRProgram *ir);

#endif