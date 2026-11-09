#ifndef STRBUF_H
#define STRBUF_H

/*
 * strbuf.h - tiny growable string buffer + string helpers (SHARED UTILITY).
 *
 * Used to build JSON text without any external library, so the compiler
 * stages stay independent of cJSON.
 *
 * Ownership: the buffer owns `data`. Call sb_free() when done, or
 * sb_detach() to take ownership of the final string (caller must free()).
 * If any allocation fails, `failed` is set and all later appends do nothing.
 */

#include <stddef.h>

#if defined(__GNUC__) || defined(__clang__)
#define STRBUF_PRINTF(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define STRBUF_PRINTF(fmt_idx, arg_idx)
#endif

typedef struct {
    char *data;   /* NUL-terminated once anything was appended (may be NULL) */
    size_t len;   /* number of characters, excluding the terminator          */
    size_t cap;   /* allocated size                                          */
    int failed;   /* non-zero after an allocation failure                    */
} StrBuf;

void sb_init(StrBuf *sb);
void sb_free(StrBuf *sb);

/* All append functions return 1 on success, 0 on failure. */
int sb_append(StrBuf *sb, const char *s);
int sb_append_n(StrBuf *sb, const char *s, size_t n);
int sb_append_char(StrBuf *sb, char c);
int sb_appendf(StrBuf *sb, const char *fmt, ...) STRBUF_PRINTF(2, 3);

/* Appends s as a quoted, escaped JSON string (including the quotes). */
int sb_append_json_string(StrBuf *sb, const char *s);

/* Never returns NULL ("" when empty). Pointer is valid until the next append. */
const char *sb_cstr(const StrBuf *sb);

/* Transfers ownership of the string to the caller (free() it).
 * Returns NULL if an allocation failed. The buffer is reset to empty. */
char *sb_detach(StrBuf *sb);

/* strdup replacement (strdup is not part of strict C11). NULL on failure. */
char *str_dup(const char *s);
char *str_dup_n(const char *s, size_t n);

#endif /* STRBUF_H */
