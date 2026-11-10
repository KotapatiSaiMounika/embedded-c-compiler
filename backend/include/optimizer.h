
#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "ir.h"
#include "diagnostics.h"

/* Optimize an existing IR program in place. */
int ir_optimize(IRProgram *ir, DiagnosticList *diags);

#endif
