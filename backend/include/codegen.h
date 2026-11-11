#ifndef CODEGEN_H
#define CODEGEN_H

/*
 * codegen.h - translates the (optimized) three-address IR into VM bytecode.
 *
 *   IR                         bytecode
 *   "+ a, b -> t0"      ==>    LOAD a / LOAD b / ADD / STORE t0
 *   "= 5 -> x"          ==>    PUSH 5 / STORE x
 *   "ifFalse t0 -> L1"  ==>    LOAD t0 / JZ <address of L1>
 *   "goto L0"           ==>    JMP <address of L0>
 *   "label L0"          ==>    (no code, only an address)
 *   end of program      ==>    HALT
 */

#include "ast.h"
#include "diagnostics.h"
#include "ir.h"
#include "vm.h"

/*
 * `ast` is optional (may be NULL). When given, every declared variable gets a
 * memory slot in declaration order, even if it never appears in the IR
 * (so "int x;" shows up as x = 0 in the memory view).
 * Returns 1 on success; on failure adds DIAG_STAGE_CODEGEN errors and returns 0.
 */
int codegen_generate(const IRProgram *ir, const ASTNode *ast,
                     VMProgram *out, DiagnosticList *diags);

#endif /* CODEGEN_H */