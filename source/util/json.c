#include "json.h"
#include <string.h>
#include <ctype.h>

void json_unescape(const char *src, char *dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0) return;

    size_t w = 0;
    for (size_t r = 0; src[r] && w + 1 < dst_size; r++) {
        if (src[r] == '\\' && src[r + 1]) {
            r++;
            switch (src[r]) {
                case '"':  dst[w++] = '"'; break;
                case '\\': dst[w++] = '\\'; break;
                case '/':  dst[w++] = '/'; break;
                case 'b':  dst[w++] = '\b'; break;
                case 'f':  dst[w++] = '\f'; break;
                case 'n':  dst[w++] = '\n'; break;
                case 'r':  dst[w++] = '\r'; break;
                case 't':  dst[w++] = '\t'; break;
                case 'u': {
                    /* Basic skip of 4 hex digits for unicode escape, replace with space or skip */
                    if (src[r+1] && src[r+2] && src[r+3] && src[r+4]) {
                        r += 4;
                    }
                    break;
                }
                default:
                    dst[w++] = src[r];
                    break;
            }
        } else {
            dst[w++] = src[r];
        }
    }
    dst[w] = '\0';
}

static const char *find_key(const char *json, const char *key) {
    if (!json || !key) return NULL;
    size_t key_len = strlen(key);

    const char *p = json;
    while ((p = strstr(p, key)) != NULL) {
        /* Check if key is surrounded by quotes: "key" */
        if (p > json && *(p - 1) == '"' && *(p + key_len) == '"') {
            const char *colon = p + key_len + 1;
            while (*colon && isspace((unsigned char)*colon)) colon++;
            if (*colon == ':') {
                return colon + 1;
            }
        }
        p += key_len;
    }
    return NULL;
}

bool json_get_string(const char *json, const char *key, char *out, size_t out_size) {
    if (!json || !key || !out || out_size == 0) return false;

    const char *val = find_key(json, key);
    if (!val) return false;

    while (*val && isspace((unsigned char)*val)) val++;
    if (*val != '"') return false;

    /* Skip leading quote */
    val++;
    const char *start = val;
    bool escaped = false;

    while (*val) {
        if (escaped) {
            escaped = false;
        } else if (*val == '\\') {
            escaped = true;
        } else if (*val == '"') {
            break;
        }
        val++;
    }

    size_t raw_len = (size_t)(val - start);
    char *temp = (char *)malloc(raw_len + 1);
    if (!temp) return false;

    strncpy(temp, start, raw_len);
    temp[raw_len] = '\0';

    json_unescape(temp, out, out_size);
    free(temp);
    return true;
}

bool json_get_int(const char *json, const char *key, long *out) {
    if (!json || !key || !out) return false;

    const char *val = find_key(json, key);
    if (!val) return false;

    while (*val && isspace((unsigned char)*val)) val++;
    if (!isdigit((unsigned char)*val) && *val != '-') return false;

    char *endptr = NULL;
    *out = strtol(val, &endptr, 10);
    return (endptr != val);
}

bool json_get_long_long(const char *json, const char *key, long long *out) {
    if (!json || !key || !out) return false;

    const char *val = find_key(json, key);
    if (!val) return false;

    while (*val && isspace((unsigned char)*val)) val++;
    if (!isdigit((unsigned char)*val) && *val != '-') return false;

    char *endptr = NULL;
    *out = strtoll(val, &endptr, 10);
    return (endptr != val);
}

bool json_get_bool(const char *json, const char *key, bool *out) {
    if (!json || !key || !out) return false;

    const char *val = find_key(json, key);
    if (!val) return false;

    while (*val && isspace((unsigned char)*val)) val++;
    if (strncmp(val, "true", 4) == 0) {
        *out = true;
        return true;
    } else if (strncmp(val, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

bool json_get_block(const char *json, const char *key, char *out, size_t out_size) {
    if (!json || !key || !out || out_size == 0) return false;

    const char *val = find_key(json, key);
    if (!val) return false;

    while (*val && isspace((unsigned char)*val)) val++;
    char open_char = *val;
    char close_char;
    if (open_char == '{') close_char = '}';
    else if (open_char == '[') close_char = ']';
    else return false;

    const char *start = val;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;

    while (*val) {
        if (in_string) {
            if (escaped) escaped = false;
            else if (*val == '\\') escaped = true;
            else if (*val == '"') in_string = false;
        } else {
            if (*val == '"') in_string = true;
            else if (*val == open_char) depth++;
            else if (*val == close_char) {
                depth--;
                if (depth == 0) {
                    val++;
                    break;
                }
            }
        }
        val++;
    }

    size_t len = (size_t)(val - start);
    if (len >= out_size) len = out_size - 1;
    strncpy(out, start, len);
    out[len] = '\0';
    return true;
}

const char *json_next_object(const char *json, const char **end_out) {
    if (end_out) *end_out = NULL;
    if (!json) return NULL;

    const char *p = json;
    while (*p && *p != '{') p++;
    if (!*p) return NULL;

    const char *start = p;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (; *p; p++) {
        char c = *p;
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
        } else {
            if (c == '"') {
                in_string = true;
            } else if (c == '{') {
                depth++;
            } else if (c == '}') {
                depth--;
                if (depth == 0) {
                    if (end_out) *end_out = p + 1;
                    return start;
                }
            }
        }
    }

    return NULL;
}

int jsonl_for_each_line(const char *content, jsonl_line_cb_t callback, void *userdata) {
    if (!content || !callback) return -1;

    const char *p = content;
    size_t line_num = 1;

    while (*p) {
        const char *line_start = p;
        while (*p && *p != '\n' && *p != '\r') p++;

        size_t len = (size_t)(p - line_start);
        if (len > 0) {
            int ret = callback(line_start, len, line_num, userdata);
            if (ret != 0) return ret;
        }

        while (*p == '\r' || *p == '\n') {
            if (*p == '\n') line_num++;
            p++;
        }
    }

    return 0;
}

void extract_tagged_text(const char *src, const char *tag, char *dst, size_t dst_size) {
    if (!src || !tag || !dst || dst_size == 0) return;

    char open_tag[64];
    char close_tag[64];
    snprintf(open_tag, sizeof(open_tag), "<%s>", tag);
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag);

    const char *start = strstr(src, open_tag);
    if (!start) {
        /* If no tag found, copy raw string */
        strncpy(dst, src, dst_size - 1);
        dst[dst_size - 1] = '\0';
        return;
    }
    start += strlen(open_tag);

    const char *end = strstr(start, close_tag);
    size_t len = end ? (size_t)(end - start) : strlen(start);
    if (len >= dst_size) len = dst_size - 1;

    strncpy(dst, start, len);
    dst[len] = '\0';
}
