#include "strbuf.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sb_init(StrBuf *sb) {
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    sb->failed = 0;
}

void sb_free(StrBuf *sb) {
    free(sb->data);
    sb_init(sb);
}

/* Make room for `extra` more characters plus the terminator. */
static int sb_reserve(StrBuf *sb, size_t extra) {
    if (sb->failed) return 0;
    if (extra > SIZE_MAX - sb->len - 1) {
        sb->failed = 1;
        return 0;
    }
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) return 1;

    size_t ncap = sb->cap ? sb->cap : 64;
    while (ncap < need) {
        if (ncap > SIZE_MAX / 2) {
            ncap = need;
            break;
        }
        ncap *= 2;
    }
    char *nd = realloc(sb->data, ncap);
    if (!nd) {
        sb->failed = 1;
        return 0;
    }
    sb->data = nd;
    sb->cap = ncap;
    return 1;
}

int sb_append_n(StrBuf *sb, const char *s, size_t n) {
    if (!sb_reserve(sb, n)) return 0;
    if (n > 0) memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 1;
}

int sb_append(StrBuf *sb, const char *s) {
    return sb_append_n(sb, s ? s : "", s ? strlen(s) : 0);
}

int sb_append_char(StrBuf *sb, char c) {
    return sb_append_n(sb, &c, 1);
}

int sb_appendf(StrBuf *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        sb->failed = 1;
        return 0;
    }
    if (!sb_reserve(sb, (size_t)n)) {
        va_end(ap2);
        return 0;
    }
    vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    sb->len += (size_t)n;
    return 1;
}

int sb_append_json_string(StrBuf *sb, const char *s) {
    if (!sb_append_char(sb, '"')) return 0;
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        unsigned char c = *p;
        switch (c) {
            case '"':  sb_append(sb, "\\\""); break;
            case '\\': sb_append(sb, "\\\\"); break;
            case '\n': sb_append(sb, "\\n"); break;
            case '\r': sb_append(sb, "\\r"); break;
            case '\t': sb_append(sb, "\\t"); break;
            default:
                if (c < 0x20 || c >= 0x7F) {
                    /* control or non-ASCII byte: always emit valid JSON */
                    sb_appendf(sb, "\\u%04x", (unsigned)c);
                } else {
                    sb_append_char(sb, (char)c);
                }
        }
    }
    return sb_append_char(sb, '"');
}

const char *sb_cstr(const StrBuf *sb) {
    return sb->data ? sb->data : "";
}

char *sb_detach(StrBuf *sb) {
    if (sb->failed) {
        sb_free(sb);
        return NULL;
    }
    char *out = sb->data;
    if (!out) out = str_dup("");
    sb->data = NULL;
    sb_init(sb);
    return out;
}

char *str_dup_n(const char *s, size_t n) {
    char *out = malloc(n + 1);
    if (!out) return NULL;
    if (n > 0) memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

char *str_dup(const char *s) {
    return str_dup_n(s ? s : "", s ? strlen(s) : 0);
}
