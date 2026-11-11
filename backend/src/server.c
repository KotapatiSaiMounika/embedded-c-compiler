/*
 * server.c - tiny REST API for the compiler (Module 3). No external libraries.
 *
 *   GET  /api/health    -> {"status":"ok"}
 *   POST /api/compile   body {"source":"int x = 2;"} -> full compiler JSON
 *
 * Run:  ./compiler_server [--port 8080] [--host 127.0.0.1]
 * One request at a time (fine for a learning project). Works on Linux/macOS/WSL
 * (POSIX sockets) and on Windows (Winsock).
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET sock_t;
  #define SOCK_INVALID INVALID_SOCKET
  #define sock_close closesocket
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <signal.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
  typedef int sock_t;
  #define SOCK_INVALID (-1)
  #define sock_close close
#endif

#include "pipeline.h"
#include "strbuf.h"

#define MAX_HEADER_BYTES 16384
#define MAX_BODY_BYTES (256 * 1024)
#define DEFAULT_PORT 8080

/* ---------------------------------------------------------- JSON body parse */

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* Parses a JSON string starting at the opening quote. Returns a malloc'd,
 * unescaped string and moves *pp past the closing quote; NULL on error. */
static char *parse_json_string(const char **pp)
{
    const char *p = *pp;
    if (*p != '"') return NULL;
    p++;

    StrBuf out;
    sb_init(&out);

    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            switch (*p) {
            case 'n': sb_append_char(&out, '\n'); break;
            case 't': sb_append_char(&out, '\t'); break;
            case 'r': sb_append_char(&out, '\r'); break;
            case 'b': sb_append_char(&out, '\b'); break;
            case 'f': sb_append_char(&out, '\f'); break;
            case '/': sb_append_char(&out, '/');  break;
            case '\\': sb_append_char(&out, '\\'); break;
            case '"': sb_append_char(&out, '"');  break;
            case 'u': {
                unsigned code = 0;
                int i;
                for (i = 1; i <= 4; i++) {
                    char h = p[i];
                    if (!isxdigit((unsigned char)h)) { sb_free(&out); return NULL; }
                    code = code * 16 + (unsigned)(isdigit((unsigned char)h) ? h - '0'
                                                                            : tolower((unsigned char)h) - 'a' + 10);
                }
                /* non-ASCII becomes '?', which the lexer then reports as an invalid character */
                sb_append_char(&out, code < 128 ? (char)code : '?');
                p += 4;
                break;
            }
            default: sb_free(&out); return NULL;
            }
            p++;
        } else {
            sb_append_char(&out, *p++);
        }
    }
    if (*p != '"') { sb_free(&out); return NULL; }
    *pp = p + 1;

    char *result = sb_detach(&out);
    return result;
}

/* Skips any JSON value (used for keys we do not care about). */
static const char *skip_value(const char *p)
{
    p = skip_ws(p);
    if (*p == '"') {
        char *s = parse_json_string(&p);
        if (!s) return NULL;
        free(s);
        return p;
    }
    int depth = 0;
    while (*p) {
        if (*p == '"') {
            const char *q = p;
            char *s = parse_json_string(&q);
            if (!s) return NULL;
            free(s);
            p = q;
            continue;
        }
        if (*p == '{' || *p == '[') depth++;
        else if (*p == '}' || *p == ']') {
            if (depth == 0) return p;
            depth--;
        } else if (*p == ',' && depth == 0) return p;
        p++;
    }
    return p;
}

/* Finds the top-level string field "source" in {"source": "..."}.
 * Returns malloc'd text or NULL if missing / invalid. */
static char *extract_source(const char *body)
{
    const char *p = skip_ws(body);
    if (*p != '{') return NULL;
    p = skip_ws(p + 1);

    while (*p && *p != '}') {
        char *key = parse_json_string(&p);
        if (!key) return NULL;
        p = skip_ws(p);
        if (*p != ':') { free(key); return NULL; }
        p = skip_ws(p + 1);

        if (strcmp(key, "source") == 0) {
            free(key);
            return *p == '"' ? parse_json_string(&p) : NULL;
        }
        free(key);

        p = skip_value(p);
        if (!p) return NULL;
        p = skip_ws(p);
        if (*p == ',') p = skip_ws(p + 1);
    }
    return NULL;
}

/* ---------------------------------------------------------------- HTTP I/O */

static int send_all(sock_t s, const char *data, size_t len)
{
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    while (len > 0) {
        int n = (int)send(s, data, (int)(len > 65536 ? 65536 : len), flags);
        if (n <= 0) return 0;
        data += n;
        len -= (size_t)n;
    }
    return 1;
}

static void send_response(sock_t s, int status, const char *status_text, const char *json)
{
    char header[512];
    size_t len = strlen(json);
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: application/json; charset=utf-8\r\n"
                     "Content-Length: %zu\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                     "Access-Control-Allow-Headers: Content-Type\r\n"
                     "Connection: close\r\n\r\n",
                     status, status_text, len);
    if (send_all(s, header, (size_t)n)) send_all(s, json, len);
}

static void send_error(sock_t s, int status, const char *status_text, const char *message)
{
    StrBuf sb;
    sb_init(&sb);
    sb_append(&sb, "{\"success\":false,\"stage\":\"request\",\"error\":");
    sb_append_json_string(&sb, message);
    sb_append(&sb, "}");
    send_response(s, status, status_text, sb_cstr(&sb));
    sb_free(&sb);
}

/* case-insensitive "starts with" (strncasecmp is not in strict C11) */
static int starts_with_ci(const char *text, const char *prefix)
{
    while (*prefix) {
        if (tolower((unsigned char)*text) != tolower((unsigned char)*prefix)) return 0;
        text++; prefix++;
    }
    return 1;
}

static long find_content_length(const char *headers)
{
    const char *p = headers;
    while (*p) {
        if (starts_with_ci(p, "content-length:")) return strtol(p + 15, NULL, 10);
        const char *nl = strstr(p, "\r\n");
        if (!nl) break;
        p = nl + 2;
    }
    return 0;
}

static void handle_client(sock_t client)
{
    char *buf = malloc(MAX_HEADER_BYTES + MAX_BODY_BYTES + 1);
    if (!buf) { send_error(client, 500, "Internal Server Error", "out of memory"); return; }

    size_t have = 0;
    char *header_end = NULL;

    /* read until the blank line that ends the headers */
    while (!header_end) {
        if (have >= MAX_HEADER_BYTES) { send_error(client, 431, "Request Header Fields Too Large", "headers too large"); free(buf); return; }
        int n = (int)recv(client, buf + have, (int)(MAX_HEADER_BYTES - have), 0);
        if (n <= 0) { free(buf); return; }
        have += (size_t)n;
        buf[have] = '\0';
        header_end = strstr(buf, "\r\n\r\n");
    }

    size_t header_len = (size_t)(header_end - buf) + 4;
    long content_length = find_content_length(buf);
    if (content_length < 0 || content_length > MAX_BODY_BYTES) {
        send_error(client, 413, "Payload Too Large", "request body too large");
        free(buf);
        return;
    }

    /* read the rest of the body */
    size_t total = header_len + (size_t)content_length;
    while (have < total) {
        int n = (int)recv(client, buf + have, (int)(total - have), 0);
        if (n <= 0) { send_error(client, 400, "Bad Request", "incomplete request body"); free(buf); return; }
        have += (size_t)n;
    }
    buf[have] = '\0';

    char method[16] = "", path[256] = "";
    if (sscanf(buf, "%15s %255s", method, path) != 2) {
        send_error(client, 400, "Bad Request", "malformed request line");
        free(buf);
        return;
    }
    char *query = strchr(path, '?');
    if (query) *query = '\0';

    const char *body = buf + header_len;
    printf("%s %s\n", method, path);
    fflush(stdout);

    if (strcmp(method, "OPTIONS") == 0) {
        send_response(client, 204, "No Content", "");
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/health") == 0) {
        send_response(client, 200, "OK", "{\"status\":\"ok\"}");
    } else if (strcmp(path, "/api/compile") == 0) {
        if (strcmp(method, "POST") != 0) {
            send_error(client, 405, "Method Not Allowed", "use POST for /api/compile");
        } else {
            char *source = extract_source(body);
            if (!source) {
                send_error(client, 400, "Bad Request", "body must be JSON like {\"source\": \"int x = 1;\"}");
            } else {
                char *json = pipeline_run_json(source, VM_DEFAULT_MAX_STEPS);
                if (json) send_response(client, 200, "OK", json);
                else send_error(client, 500, "Internal Server Error", "out of memory");
                free(json);
                free(source);
            }
        }
    } else {
        send_error(client, 404, "Not Found", "unknown endpoint");
    }
    free(buf);
}

/* -------------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    int port = DEFAULT_PORT;
    const char *host = "127.0.0.1";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) host = argv[++i];
        else { fprintf(stderr, "usage: %s [--port N] [--host IP]\n", argv[0]); return 2; }
    }
    if (port < 1 || port > 65535) { fprintf(stderr, "invalid port\n"); return 2; }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { fprintf(stderr, "WSAStartup failed\n"); return 1; }
#else
    signal(SIGPIPE, SIG_IGN);
#endif

    sock_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == SOCK_INVALID) { fprintf(stderr, "socket() failed\n"); return 1; }

    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = inet_addr(host);
    if (addr.sin_addr.s_addr == INADDR_NONE) { fprintf(stderr, "invalid host '%s'\n", host); return 2; }

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "bind() failed - is port %d already in use?\n", port);
        return 1;
    }
    if (listen(server, 16) != 0) { fprintf(stderr, "listen() failed\n"); return 1; }

    printf("Compiler API listening on http://%s:%d\n", host, port);
    printf("  GET  /api/health\n  POST /api/compile   {\"source\": \"...\"}\n");
    fflush(stdout);

    for (;;) {
        sock_t client = accept(server, NULL, NULL);
        if (client == SOCK_INVALID) continue;

        /* do not let a stalled client block the server forever (5 s) */
#ifdef _WIN32
        DWORD timeout = 5000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
#else
        struct timeval timeout = { 5, 0 };
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
#endif
        handle_client(client);
        sock_close(client);
    }
}