#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

/*
 * diagnostics.h - error/warning reporting shared by EVERY compiler stage
 * (SHARED INTERFACE - Person 2 and Person 3 use it too).
 *
 * A DiagnosticList is created by the caller (compiler orchestrator), passed to
 * each stage, and freed once with diag_list_free().
 */

#include "strbuf.h"

#if defined(__GNUC__) || defined(__clang__)
#define DIAG_PRINTF(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define DIAG_PRINTF(fmt_idx, arg_idx)
#endif

#define DIAG_MESSAGE_SIZE 256
#define DIAG_MAX_COUNT 100 /* extra messages are dropped to avoid flooding */

typedef enum {
    DIAG_STAGE_LEXICAL,
    DIAG_STAGE_SYNTAX,
    DIAG_STAGE_SEMANTIC,
    DIAG_STAGE_IR,
    DIAG_STAGE_OPTIMIZER,
    DIAG_STAGE_CODEGEN,
    DIAG_STAGE_RUNTIME
} DiagStage;

typedef enum {
    DIAG_ERROR,
    DIAG_WARNING,
    DIAG_INFO
} DiagSeverity;

typedef struct {
    DiagStage stage;
    DiagSeverity severity;
    int line;   /* 1-based, 0 if unknown */
    int column; /* 1-based, 0 if unknown */
    char message[DIAG_MESSAGE_SIZE];
} Diagnostic;

typedef struct {
    Diagnostic *items;
    int count;
    int capacity;
} DiagnosticList;

void diag_list_init(DiagnosticList *list);
void diag_list_free(DiagnosticList *list);

/* printf-style message. Returns 1 if stored, 0 if dropped / out of memory. */
int diag_list_add(DiagnosticList *list, DiagStage stage, DiagSeverity severity,
                  int line, int column, const char *fmt, ...) DIAG_PRINTF(6, 7);

/* Number of diagnostics whose severity is DIAG_ERROR. */
int diag_list_error_count(const DiagnosticList *list);

const char *diag_stage_name(DiagStage stage);       /* "lexical", "syntax", ... */
const char *diag_severity_name(DiagSeverity sev);   /* "error", "warning", "info" */

/* JSON array: [{"stage","severity","message","line","column"}, ...] */
void diag_list_write_json(const DiagnosticList *list, StrBuf *sb);
char *diag_list_to_json(const DiagnosticList *list); /* caller frees; NULL on OOM */

#endif /* DIAGNOSTICS_H */
