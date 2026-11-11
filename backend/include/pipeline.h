#ifndef PIPELINE_H
#define PIPELINE_H

/*
 * pipeline.h - runs the WHOLE compiler (Modules 1, 2 and 3) on one source string.
 *
 *   lexer -> parser -> semantic -> checks -> IR -> optimizer -> codegen -> VM
 *
 * Used by the command-line tool and by the REST server, so both behave the same.
 */

#include "ast.h"
#include "codegen.h"
#include "diagnostics.h"
#include "ir.h"
#include "optimizer.h"
#include "token.h"
#include "vm.h"

#define PIPELINE_TRACE_MAX 2000 /* trace entries sent to the UI (execution continues past it) */

typedef struct {
    TokenArray tokens;
    ASTNode *ast;
    DiagnosticList diags;
    IRProgram ir_raw;       /* IR before optimization */
    IRProgram ir_opt;       /* IR after optimization  */
    VMProgram code;         /* bytecode               */
    int failed;             /* 1 if a compile stage failed            */
    DiagStage failed_stage; /* valid when failed == 1                 */
    int compiled;           /* 1 when bytecode is ready to execute    */
} PipelineResult;

void pipeline_init(PipelineResult *r);
void pipeline_free(PipelineResult *r);

/* Runs every compile stage. Returns 1 if bytecode was produced, 0 otherwise
 * (see r->diags and r->failed_stage). */
int pipeline_compile(const char *source, PipelineResult *r);

/*
 * Executes the bytecode (if compiled) and returns the complete JSON response
 * for the frontend. Runtime errors are added to r->diags.
 * Caller free()s the result. NULL on out-of-memory.
 */
char *pipeline_to_json(PipelineResult *r, long max_steps);

/* Convenience: init + compile + to_json + free. */
char *pipeline_run_json(const char *source, long max_steps);

#endif /* PIPELINE_H */