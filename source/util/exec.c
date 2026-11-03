#include "exec.h"
#include <string.h>

#ifdef _WIN32
  #define popen _popen
  #define pclose _pclose
#endif

char *exec_capture(const char *cmd) {
    if (!cmd || cmd[0] == '\0') return NULL;

    size_t wrapped_size = strlen(cmd) + 16;
    char *wrapped = (char *)malloc(wrapped_size);
    if (!wrapped) return NULL;

#ifdef _WIN32
    snprintf(wrapped, wrapped_size, "%s 2>nul", cmd);
#else
    snprintf(wrapped, wrapped_size, "%s 2>/dev/null", cmd);
#endif

    FILE *pipe = popen(wrapped, "r");
    free(wrapped);
    if (!pipe) return NULL;

    size_t capacity = 16384;
    size_t length = 0;
    char *buffer = (char *)malloc(capacity);
    if (!buffer) {
        pclose(pipe);
        return NULL;
    }

    size_t read;
    while ((read = fread(buffer + length, 1, capacity - length - 1, pipe)) > 0) {
        length += read;
        if (length + 1 >= capacity) {
            size_t new_capacity = capacity * 2;
            char *grown = (char *)realloc(buffer, new_capacity);
            if (!grown) {
                free(buffer);
                pclose(pipe);
                return NULL;
            }
            buffer = grown;
            capacity = new_capacity;
        }
    }

    buffer[length] = '\0';
    pclose(pipe);
    return buffer;
}

bool exec_command_exists(const char *name) {
    if (!name || name[0] == '\0') return false;

    char cmd[256];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "where %s >nul 2>&1", name);
#else
    snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", name);
#endif
    return system(cmd) == 0;
}

void exec_shell_quote(const char *in, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!in) return;

    size_t w = 0;
#ifdef _WIN32
    if (w + 1 < out_size) out[w++] = '"';
    for (const char *p = in; *p && w + 2 < out_size; p++) {
        if (*p == '"') {
            if (w + 2 >= out_size) break;
            out[w++] = '\\';
        }
        out[w++] = *p;
    }
    if (w + 1 < out_size) out[w++] = '"';
#else
    if (w + 1 < out_size) out[w++] = '\'';
    for (const char *p = in; *p && w + 5 < out_size; p++) {
        if (*p == '\'') {
            memcpy(out + w, "'\\''", 4);
            w += 4;
        } else {
            out[w++] = *p;
        }
    }
    if (w + 2 < out_size) out[w++] = '\'';
#endif
    out[w] = '\0';
}
