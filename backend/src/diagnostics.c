#include "diagnostics.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void diag_list_init(DiagnosticList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void diag_list_free(DiagnosticList *list) {
    free(list->items);
    diag_list_init(list);
}

int diag_list_add(DiagnosticList *list, DiagStage stage, DiagSeverity severity,
                  int line, int column, const char *fmt, ...) {
    if (!list || list->count >= DIAG_MAX_COUNT) return 0;

    if (list->count == list->capacity) {
        int ncap = list->capacity ? list->capacity * 2 : 8;
        Diagnostic *ni = realloc(list->items, (size_t)ncap * sizeof(Diagnostic));
        if (!ni) return 0;
        list->items = ni;
        list->capacity = ncap;
    }

    Diagnostic *d = &list->items[list->count];
    d->stage = stage;
    d->severity = severity;
    d->line = line;
    d->column = column;

    if (list->count == DIAG_MAX_COUNT - 1) {
        snprintf(d->message, DIAG_MESSAGE_SIZE,
                 "Too many diagnostics; further messages are suppressed");
    } else {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(d->message, DIAG_MESSAGE_SIZE, fmt, ap);
        va_end(ap);
    }
    list->count++;
    return 1;
}

int diag_list_error_count(const DiagnosticList *list) {
    int n = 0;
    if (!list) return 0;
    for (int i = 0; i < list->count; i++) {
        if (list->items[i].severity == DIAG_ERROR) n++;
    }
    return n;
}

const char *diag_stage_name(DiagStage stage) {
    switch (stage) {
        case DIAG_STAGE_LEXICAL:   return "lexical";
        case DIAG_STAGE_SYNTAX:    return "syntax";
        case DIAG_STAGE_SEMANTIC:  return "semantic";
        case DIAG_STAGE_IR:        return "ir";
        case DIAG_STAGE_OPTIMIZER: return "optimizer";
        case DIAG_STAGE_CODEGEN:   return "codegen";
        case DIAG_STAGE_RUNTIME:   return "runtime";
    }
    return "unknown";
}

const char *diag_severity_name(DiagSeverity sev) {
    switch (sev) {
        case DIAG_ERROR:   return "error";
        case DIAG_WARNING: return "warning";
        case DIAG_INFO:    return "info";
    }
    return "unknown";
}

void diag_list_write_json(const DiagnosticList *list, StrBuf *sb) {
    sb_append_char(sb, '[');
    for (int i = 0; list && i < list->count; i++) {
        const Diagnostic *d = &list->items[i];
        if (i > 0) sb_append_char(sb, ',');
        sb_append(sb, "{\"stage\":");
        sb_append_json_string(sb, diag_stage_name(d->stage));
        sb_append(sb, ",\"severity\":");
        sb_append_json_string(sb, diag_severity_name(d->severity));
        sb_append(sb, ",\"message\":");
        sb_append_json_string(sb, d->message);
        sb_appendf(sb, ",\"line\":%d,\"column\":%d}", d->line, d->column);
    }
    sb_append_char(sb, ']');
}

char *diag_list_to_json(const DiagnosticList *list) {
    StrBuf sb;
    sb_init(&sb);
    diag_list_write_json(list, &sb);
    return sb_detach(&sb);
}
