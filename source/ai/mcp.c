#include "mcp.h"
#include "../config/config.h"
#include "../util/exec.h"
#include "../util/fs.h"
#include "../util/json.h"
#include <ctype.h>
#include <string.h>

#ifdef _WIN32
  #include <direct.h>
  #define getcwd _getcwd
#else
  #include <unistd.h>
#endif

/* MCP focus uses the same host the user was most recently working in. If that
 * host has no configured servers, Flint falls through the other installed
 * adapters. There is deliberately no built-in preference for one CLI. */

typedef struct {
    const char *id;
    const char *binary;
    const char *name;
} mcp_host_info_t;

static const mcp_host_info_t MCP_HOSTS[] = {
    { "agy",      "agy",      "Antigravity CLI" },
    { "claude",   "claude",   "Claude Code" },
    { "cline",    "cline",    "Cline" },
    { "codex",    "codex",    "ChatGPT Codex" },
    { "opencode", "opencode", "OpenCode" },
    { "openclaw", "openclaw", "OpenClaw" }
};

#define MCP_HOST_COUNT (sizeof(MCP_HOSTS) / sizeof(MCP_HOSTS[0]))
#define MCP_MAX_WORKSPACES 8

typedef struct {
    char text[8192];
    size_t len;
    char session_id[MAX_NAME_LEN];
    bool have_final;
} mcp_text_ctx_t;

static bool mcp_id_equals(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int mcp_host_index(const char *id) {
    if (!id || id[0] == '\0' || mcp_id_equals(id, "auto")) return -1;
    for (size_t i = 0; i < MCP_HOST_COUNT; i++) {
        if (mcp_id_equals(id, MCP_HOSTS[i].id) ||
            mcp_id_equals(id, MCP_HOSTS[i].name)) {
            return (int)i;
        }
    }
    return -1;
}

static bool csv_contains(const char *csv, const char *name) {
    if (!csv || !name || name[0] == '\0') return false;

    size_t name_len = strlen(name);
    const char *p = csv;
    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        const char *start = p;
        while (*p && *p != ',') p++;

        size_t len = (size_t)(p - start);
        while (len > 0 && start[len - 1] == ' ') len--;
        if (len == name_len && strncmp(start, name, name_len) == 0) {
            return true;
        }
    }
    return false;
}

static void append_server(char *out, size_t out_size, const char *name) {
    if (!out || out_size == 0 || !name || name[0] == '\0') return;
    if (csv_contains(out, name)) return;

    size_t used = strlen(out);
    if (used > 0 && used + 2 < out_size) {
        out[used++] = ',';
        out[used++] = ' ';
        out[used] = '\0';
    }

    size_t name_len = strlen(name);
    if (used + name_len >= out_size) {
        name_len = out_size - used - 1;
    }
    memcpy(out + used, name, name_len);
    out[used + name_len] = '\0';
}

static bool mcp_json_has_nonempty_block(const char *json, const char *key) {
    if (!json || !key) return false;

    size_t size = strlen(json) + 1;
    char *block = (char *)malloc(size);
    if (!block) return false;

    bool found = false;
    block[0] = '\0';
    if (json_get_block(json, key, block, size)) {
        const char *start = block;
        while (*start && isspace((unsigned char)*start)) start++;
        size_t len = strlen(start);
        while (len > 0 && isspace((unsigned char)start[len - 1])) len--;
        found = len > 2;
    }

    free(block);
    return found;
}

static bool mcp_file_has_nonempty_block(const char *path, const char *key) {
    size_t size = 0;
    char *content = fs_read_file(path, &size);
    if (!content) return false;

    bool found = mcp_json_has_nonempty_block(content, key);
    free(content);
    return found;
}

static bool mcp_file_has_nested_block(const char *path, const char *outer, const char *inner) {
    size_t size = 0;
    char *content = fs_read_file(path, &size);
    if (!content) return false;

    size_t block_size = strlen(content) + 1;
    char *block = (char *)malloc(block_size);
    bool found = false;
    if (block) {
        block[0] = '\0';
        if (json_get_block(content, outer, block, block_size)) {
            found = mcp_json_has_nonempty_block(block, inner);
        }
        free(block);
    }

    free(content);
    return found;
}

static bool mcp_file_has_content(const char *path) {
    size_t size = 0;
    char *content = fs_read_file(path, &size);
    if (!content) return false;

    bool found = false;
    for (size_t i = 0; i < size; i++) {
        if (!isspace((unsigned char)content[i])) {
            found = true;
            break;
        }
    }

    free(content);
    return found;
}

static bool mcp_host_may_have_servers(const char *host_id, const char *workspace) {
    if (mcp_id_equals(host_id, "opencode")) {
        return true;
    }

    if (mcp_id_equals(host_id, "agy")) {
        char path[MAX_PATH_LEN];
        fs_expand_path("~/.gemini/config/mcp_config.json", path, sizeof(path));
        return mcp_file_has_nonempty_block(path, "mcpServers");
    }

    if (mcp_id_equals(host_id, "cline")) {
        char path[MAX_PATH_LEN];
        const char *override = getenv("CLINE_MCP_SETTINGS_PATH");
        if (override && override[0] != '\0') {
            fs_expand_path(override, path, sizeof(path));
        } else {
            fs_expand_path("~/.cline/data/settings/cline_mcp_settings.json", path, sizeof(path));
        }
        return mcp_file_has_nonempty_block(path, "mcpServers");
    }

    if (mcp_id_equals(host_id, "openclaw")) {
        char path[MAX_PATH_LEN];
        fs_expand_path("~/.openclaw/openclaw.json", path, sizeof(path));
        return mcp_file_has_nested_block(path, "mcp", "servers");
    }

    if (mcp_id_equals(host_id, "claude")) {
        char path[MAX_PATH_LEN];
        fs_expand_path("~/.claude.json", path, sizeof(path));
        if (mcp_file_has_nonempty_block(path, "mcpServers")) {
            return true;
        }

        if (workspace && workspace[0] != '\0') {
            fs_path_join(workspace, ".mcp.json", path, sizeof(path));
            if (mcp_file_has_nonempty_block(path, "mcpServers")) {
                return true;
            }
        }

        return false;
    }

    if (mcp_id_equals(host_id, "codex")) {
        char codex_home[MAX_PATH_LEN];
        char path[MAX_PATH_LEN];
        const char *env_home = getenv("CODEX_HOME");
        if (env_home && env_home[0] != '\0') {
            strncpy(codex_home, env_home, sizeof(codex_home) - 1);
            codex_home[sizeof(codex_home) - 1] = '\0';
        } else {
            fs_expand_path("~/.codex", codex_home, sizeof(codex_home));
        }
        fs_path_join(codex_home, "config.toml", path, sizeof(path));
        return mcp_file_has_content(path);
    }

    return true;
}

static bool mcp_host_needs_workspace(const char *host_id) {
    return mcp_id_equals(host_id, "opencode");
}

bool mcp_parse_connected_servers(const char *json, char *out, size_t out_size) {
    if (!json || !out || out_size == 0) return false;
    out[0] = '\0';

    size_t json_len = strlen(json);
    char *data = (char *)malloc(json_len + 1);
    const char *scan = json;

    if (data) {
        data[0] = '\0';
        if (json_get_block(json, "data", data, json_len + 1)) {
            scan = data;
        }
    }

    const char *p = scan;
    while (true) {
        const char *end = NULL;
        const char *object = json_next_object(p, &end);
        if (!object) break;

        size_t len = (size_t)(end - object);
        char *slice = (char *)malloc(len + 1);
        if (!slice) break;

        memcpy(slice, object, len);
        slice[len] = '\0';

        char name[128] = {0};
        if (json_get_string(slice, "name", name, sizeof(name))) {
            bool connected = false;

            char status_block[1024] = {0};
            if (json_get_block(slice, "status", status_block, sizeof(status_block))) {
                char status[64] = {0};
                if (json_get_string(status_block, "status", status, sizeof(status))) {
                    connected = strcmp(status, "connected") == 0;
                }
            }

            if (!connected) {
                char status[64] = {0};
                if (json_get_string(slice, "status", status, sizeof(status))) {
                    connected = strcmp(status, "connected") == 0;
                }
            }

            if (connected) {
                append_server(out, out_size, name);
            }
        }

        free(slice);
        p = end;
    }

    free(data);
    return out[0] != '\0';
}

static void mcp_parse_json_server_array(const char *json, const char *array_key,
                                        bool codex_style,
                                        char *out, size_t out_size) {
    if (!json || !out || out_size == 0) return;

    char *block = NULL;
    const char *scan = json;
    if (array_key && array_key[0] != '\0') {
        size_t size = strlen(json) + 1;
        block = (char *)malloc(size);
        if (!block) return;
        block[0] = '\0';
        if (!json_get_block(json, array_key, block, size)) {
            free(block);
            return;
        }
        scan = block;
    }

    const char *p = scan;
    while (true) {
        const char *end = NULL;
        const char *object = json_next_object(p, &end);
        if (!object) break;

        size_t len = (size_t)(end - object);
        char *slice = (char *)malloc(len + 1);
        if (!slice) break;
        memcpy(slice, object, len);
        slice[len] = '\0';

        char name[MAX_NAME_LEN] = {0};
        if (json_get_string(slice, "name", name, sizeof(name))) {
            bool enabled = true;
            bool disabled = false;
            bool ok = true;
            bool configured = true;

            json_get_bool(slice, "enabled", &enabled);
            if (json_get_bool(slice, "disabled", &disabled)) {
                enabled = !disabled;
            }
            json_get_bool(slice, "ok", &ok);
            json_get_bool(slice, "configured", &configured);

            bool usable = enabled && ok && configured;
            if (usable && codex_style) {
                char auth_status[64] = {0};
                if (json_get_string(slice, "auth_status", auth_status, sizeof(auth_status)) &&
                    strcmp(auth_status, "not_logged_in") == 0) {
                    usable = false;
                }
            }

            if (usable) {
                append_server(out, out_size, name);
            }
        }

        free(slice);
        p = end;
    }

    free(block);
}

static void mcp_parse_table_servers(const char *text, char *out, size_t out_size) {
    if (!text || !out || out_size == 0) return;

    const char *p = text;
    while (*p) {
        const char *start = p;
        while (*p && *p != '\n' && *p != '\r') p++;

        size_t len = (size_t)(p - start);
        char line[1024];
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, start, len);
        line[len] = '\0';
        str_trim(line);

        if (line[0] == '\0' ||
            str_has_prefix(line, "NAME") ||
            str_has_prefix(line, "Name") ||
            str_has_prefix(line, "No MCP") ||
            str_has_prefix(line, "Error") ||
            str_has_prefix(line, "Usage")) {
            while (*p == '\r' || *p == '\n') p++;
            continue;
        }

        if (str_contains_ignore_case(line, "disabled")) {
            while (*p == '\r' || *p == '\n') p++;
            continue;
        }

        const char *end = line;
        while (*end && !isspace((unsigned char)*end) && *end != ':') end++;
        size_t name_len = (size_t)(end - line);
        if (name_len > 0 && name_len < MAX_NAME_LEN) {
            char name[MAX_NAME_LEN];
            memcpy(name, line, name_len);
            name[name_len] = '\0';
            append_server(out, out_size, name);
        }

        while (*p == '\r' || *p == '\n') p++;
    }
}

static void mcp_parse_claude_servers(const char *text, char *out, size_t out_size) {
    if (!text || !out || out_size == 0) return;
    if (strstr(text, "No MCP servers configured") != NULL) return;

    const char *p = text;
    while (*p) {
        const char *start = p;
        while (*p && *p != '\n' && *p != '\r') p++;

        size_t len = (size_t)(p - start);
        char line[2048];
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, start, len);
        line[len] = '\0';
        str_trim(line);

        if (line[0] == '\0' ||
            str_has_prefix(line, "Checking") ||
            str_has_prefix(line, "No MCP") ||
            str_contains_ignore_case(line, "Failed") ||
            str_contains_ignore_case(line, "Needs authentication") ||
            str_contains_ignore_case(line, "Pending approval") ||
            str_contains_ignore_case(line, "Rejected") ||
            str_contains_ignore_case(line, "Disabled") ||
            str_contains_ignore_case(line, "Blocked")) {
            while (*p == '\r' || *p == '\n') p++;
            continue;
        }

        bool healthy = str_contains_ignore_case(line, "Connected") ||
                       str_contains_ignore_case(line, "cached");
        if (!healthy) {
            while (*p == '\r' || *p == '\n') p++;
            continue;
        }

        const char *end = line;
        while (*end && *end != ':' && !isspace((unsigned char)*end)) end++;
        size_t name_len = (size_t)(end - line);
        if (name_len > 0 && name_len < MAX_NAME_LEN) {
            char name[MAX_NAME_LEN];
            memcpy(name, line, name_len);
            name[name_len] = '\0';
            append_server(out, out_size, name);
        }

        while (*p == '\r' || *p == '\n') p++;
    }
}

static void mcp_parse_servers(const char *host_id, const char *output,
                              char *out, size_t out_size) {
    if (!output || !out || out_size == 0) return;
    out[0] = '\0';

    if (mcp_id_equals(host_id, "agy")) {
        mcp_parse_table_servers(output, out, out_size);
    } else if (mcp_id_equals(host_id, "claude")) {
        mcp_parse_claude_servers(output, out, out_size);
    } else if (mcp_id_equals(host_id, "cline")) {
        mcp_parse_json_server_array(output, NULL, false, out, out_size);
    } else if (mcp_id_equals(host_id, "codex")) {
        mcp_parse_json_server_array(output, NULL, true, out, out_size);
    } else if (mcp_id_equals(host_id, "opencode")) {
        mcp_parse_connected_servers(output, out, out_size);
    } else if (mcp_id_equals(host_id, "openclaw")) {
        mcp_parse_json_server_array(output, "servers", false, out, out_size);
    }
}

static void mcp_text_append(mcp_text_ctx_t *ctx, const char *text, bool replace) {
    if (!ctx || !text || text[0] == '\0') return;
    if (replace) {
        ctx->len = 0;
        ctx->text[0] = '\0';
    }
    size_t text_len = strlen(text);
    if (ctx->len + text_len + 2 < sizeof(ctx->text)) {
        if (ctx->len > 0) {
            ctx->text[ctx->len++] = '\n';
        }
        memcpy(ctx->text + ctx->len, text, text_len);
        ctx->len += text_len;
        ctx->text[ctx->len] = '\0';
    }
}

static void mcp_json_text(const char *json, const char *const *keys, size_t key_count,
                          mcp_text_ctx_t *ctx) {
    if (!json || !ctx) return;
    for (size_t i = 0; i < key_count; i++) {
        char text[8192] = {0};
        if (json_get_string(json, keys[i], text, sizeof(text)) && text[0] != '\0') {
            mcp_text_append(ctx, text, false);
            return;
        }
    }
}

static int mcp_collect_opencode_line(const char *line, size_t len, size_t line_num, void *userdata) {
    (void)line_num;
    mcp_text_ctx_t *ctx = (mcp_text_ctx_t *)userdata;

    char *buf = (char *)malloc(len + 1);
    if (!buf) return 0;
    memcpy(buf, line, len);
    buf[len] = '\0';

    char session_id[MAX_NAME_LEN] = {0};
    if (ctx->session_id[0] == '\0' &&
        json_get_string(buf, "sessionID", session_id, sizeof(session_id))) {
        strncpy(ctx->session_id, session_id, sizeof(ctx->session_id) - 1);
    }

    char type[32] = {0};
    if (json_get_string(buf, "type", type, sizeof(type)) &&
        strcmp(type, "text") == 0) {
        char part[8192] = {0};
        if (json_get_block(buf, "part", part, sizeof(part))) {
            char text[8192] = {0};
            if (json_get_string(part, "text", text, sizeof(text))) {
                mcp_text_append(ctx, text, false);
            }
        }
    }

    free(buf);
    return 0;
}

#ifdef TEST_MODE
int flint_mcp_collect_line(const char *line, size_t len, void *ctx) {
    return mcp_collect_opencode_line(line, len, 0, ctx);
}
#endif

static int mcp_collect_cline_line(const char *line, size_t len, size_t line_num, void *userdata) {
    (void)line_num;
    mcp_text_ctx_t *ctx = (mcp_text_ctx_t *)userdata;

    char *buf = (char *)malloc(len + 1);
    if (!buf) return 0;
    memcpy(buf, line, len);
    buf[len] = '\0';

    char session_id[MAX_NAME_LEN] = {0};
    if (ctx->session_id[0] == '\0') {
        if (!json_get_string(buf, "sessionId", session_id, sizeof(session_id))) {
            if (!json_get_string(buf, "sessionID", session_id, sizeof(session_id))) {
                json_get_string(buf, "session_id", session_id, sizeof(session_id));
            }
        }
        if (session_id[0] != '\0') {
            strncpy(ctx->session_id, session_id, sizeof(ctx->session_id) - 1);
        }
    }

    char type[64] = {0};
    if (!json_get_string(buf, "type", type, sizeof(type))) {
        free(buf);
        return 0;
    }

    if (strcmp(type, "run_result") == 0) {
        char text[8192] = {0};
        if (json_get_string(buf, "text", text, sizeof(text))) {
            mcp_text_append(ctx, text, true);
            ctx->have_final = true;
        }
    } else if (!ctx->have_final && strcmp(type, "agent_event") == 0) {
        char event[8192] = {0};
        if (json_get_block(buf, "event", event, sizeof(event))) {
            char event_type[64] = {0};
            if (json_get_string(event, "type", event_type, sizeof(event_type)) &&
                strcmp(event_type, "done") == 0) {
                char text[8192] = {0};
                if (json_get_string(event, "text", text, sizeof(text))) {
                    mcp_text_append(ctx, text, true);
                }
            }
        }
    }

    free(buf);
    return 0;
}

static int mcp_collect_codex_line(const char *line, size_t len, size_t line_num, void *userdata) {
    (void)line_num;
    mcp_text_ctx_t *ctx = (mcp_text_ctx_t *)userdata;

    char *buf = (char *)malloc(len + 1);
    if (!buf) return 0;
    memcpy(buf, line, len);
    buf[len] = '\0';

    char type[64] = {0};
    if (json_get_string(buf, "type", type, sizeof(type))) {
        if (strcmp(type, "thread.started") == 0) {
            char session_id[MAX_NAME_LEN] = {0};
            if (!json_get_string(buf, "thread_id", session_id, sizeof(session_id))) {
                json_get_string(buf, "threadId", session_id, sizeof(session_id));
            }
            if (session_id[0] != '\0') {
                strncpy(ctx->session_id, session_id, sizeof(ctx->session_id) - 1);
            }
        } else if (strcmp(type, "item.completed") == 0) {
            char item[8192] = {0};
            if (json_get_block(buf, "item", item, sizeof(item))) {
                char item_type[64] = {0};
                if (json_get_string(item, "type", item_type, sizeof(item_type)) &&
                    (strcmp(item_type, "agent_message") == 0 ||
                     strcmp(item_type, "assistant_message") == 0)) {
                    char text[8192] = {0};
                    if (json_get_string(item, "text", text, sizeof(text))) {
                        mcp_text_append(ctx, text, false);
                    }
                }
            }
        }
    }

    free(buf);
    return 0;
}

static void mcp_parse_result(const char *host_id, const char *output,
                             char *out, size_t out_size, char *session_id_out) {
    if (!output || !out || out_size == 0) return;
    out[0] = '\0';
    if (session_id_out) session_id_out[0] = '\0';

    mcp_text_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    if (mcp_id_equals(host_id, "opencode")) {
        jsonl_for_each_line(output, mcp_collect_opencode_line, &ctx);
    } else if (mcp_id_equals(host_id, "cline")) {
        jsonl_for_each_line(output, mcp_collect_cline_line, &ctx);
    } else if (mcp_id_equals(host_id, "codex")) {
        jsonl_for_each_line(output, mcp_collect_codex_line, &ctx);
    } else if (mcp_id_equals(host_id, "openclaw")) {
        static const char *const keys[] = { "final", "result", "text" };
        mcp_json_text(output, keys, sizeof(keys) / sizeof(keys[0]), &ctx);
        if (ctx.text[0] == '\0') {
            char payloads[8192] = {0};
            if (json_get_block(output, "payloads", payloads, sizeof(payloads))) {
                const char *end = NULL;
                const char *object = json_next_object(payloads, &end);
                if (object) {
                    char slice[4096];
                    size_t len = (size_t)(end - object);
                    if (len >= sizeof(slice)) len = sizeof(slice) - 1;
                    memcpy(slice, object, len);
                    slice[len] = '\0';
                    char text[8192] = {0};
                    if (json_get_string(slice, "text", text, sizeof(text))) {
                        mcp_text_append(&ctx, text, false);
                    }
                }
            }
        }
        char session_id[MAX_NAME_LEN] = {0};
        if (!json_get_string(output, "sessionId", session_id, sizeof(session_id))) {
            json_get_string(output, "session_id", session_id, sizeof(session_id));
        }
        if (session_id[0] != '\0') {
            strncpy(ctx.session_id, session_id, sizeof(ctx.session_id) - 1);
        }
    } else if (mcp_id_equals(host_id, "claude")) {
        static const char *const keys[] = { "result", "text", "response" };
        mcp_json_text(output, keys, sizeof(keys) / sizeof(keys[0]), &ctx);
        char session_id[MAX_NAME_LEN] = {0};
        if (!json_get_string(output, "session_id", session_id, sizeof(session_id))) {
            json_get_string(output, "sessionId", session_id, sizeof(session_id));
        }
        if (session_id[0] != '\0') {
            strncpy(ctx.session_id, session_id, sizeof(ctx.session_id) - 1);
        }
    } else if (mcp_id_equals(host_id, "agy")) {
        static const char *const keys[] = { "result", "text", "response", "content", "message" };
        mcp_json_text(output, keys, sizeof(keys) / sizeof(keys[0]), &ctx);
        char session_id[MAX_NAME_LEN] = {0};
        if (!json_get_string(output, "session_id", session_id, sizeof(session_id))) {
            if (!json_get_string(output, "sessionId", session_id, sizeof(session_id))) {
                if (!json_get_string(output, "conversationId", session_id, sizeof(session_id))) {
                    json_get_string(output, "conversation_id", session_id, sizeof(session_id));
                }
            }
        }
        if (session_id[0] != '\0') {
            strncpy(ctx.session_id, session_id, sizeof(ctx.session_id) - 1);
        }
    }

    str_trim(ctx.text);
    if (ctx.text[0] == '\0') return;

    strncpy(out, ctx.text, out_size - 1);
    out[out_size - 1] = '\0';
    if (session_id_out) {
        strncpy(session_id_out, ctx.session_id, MAX_NAME_LEN - 1);
        session_id_out[MAX_NAME_LEN - 1] = '\0';
    }
}

static void mcp_remove_opencode_session(const char *session_id) {
    if (!session_id || session_id[0] == '\0') return;

    char param[MAX_NAME_LEN + 32];
    snprintf(param, sizeof(param), "sessionID=%s", session_id);

    char quoted_param[MAX_NAME_LEN * 2 + 64];
    exec_shell_quote(param, quoted_param, sizeof(quoted_param));

    char cmd[MAX_NAME_LEN * 3 + 128];
    snprintf(cmd, sizeof(cmd), "opencode api session.remove --param %s", quoted_param);

    char *ignored = exec_capture(cmd);
    free(ignored);
}

static bool mcp_build_prompt_command(const char *host_id, const char *workspace,
                                     const char *prompt, char *out, size_t out_size) {
    if (!host_id || !workspace || !prompt || !out || out_size == 0) return false;
    out[0] = '\0';

    int host = mcp_host_index(host_id);
    if (host < 0) return false;

    size_t quoted_prompt_size = strlen(prompt) * 4 + 8;
    size_t quoted_workspace_size = strlen(workspace) * 4 + 8;
    char *quoted_prompt = (char *)malloc(quoted_prompt_size);
    char *quoted_workspace = (char *)malloc(quoted_workspace_size);
    if (!quoted_prompt || !quoted_workspace) {
        free(quoted_prompt);
        free(quoted_workspace);
        return false;
    }

    exec_shell_quote(prompt, quoted_prompt, quoted_prompt_size);
    exec_shell_quote(workspace, quoted_workspace, quoted_workspace_size);

    if (mcp_id_equals(host_id, "agy")) {
        snprintf(out, out_size,
                 "cd %s && agy -p --output-format json --mode plan %s",
                 quoted_workspace, quoted_prompt);
    } else if (mcp_id_equals(host_id, "claude")) {
        snprintf(out, out_size,
                 "cd %s && claude -p --output-format json --permission-mode plan "
                 "--no-session-persistence %s",
                 quoted_workspace, quoted_prompt);
    } else if (mcp_id_equals(host_id, "cline")) {
        snprintf(out, out_size,
                 "cd %s && cline -p --json --auto-approve true %s",
                 quoted_workspace, quoted_prompt);
    } else if (mcp_id_equals(host_id, "codex")) {
        snprintf(out, out_size,
                 "cd %s && codex --ask-for-approval never exec --json --ephemeral "
                 "--sandbox read-only %s",
                 quoted_workspace, quoted_prompt);
    } else if (mcp_id_equals(host_id, "opencode")) {
        snprintf(out, out_size,
                 "cd %s && opencode run --agent plan --auto --format json "
                 "--title 'Flint focus query' %s",
                 quoted_workspace, quoted_prompt);
    } else if (mcp_id_equals(host_id, "openclaw")) {
        snprintf(out, out_size,
                 "cd %s && openclaw agent exec --json --cwd %s %s",
                 quoted_workspace, quoted_workspace, quoted_prompt);
    }

    free(quoted_prompt);
    free(quoted_workspace);
    return out[0] != '\0';
}

static bool mcp_run_prompt_for_host(const char *host_id, const char *workspace,
                                    const char *prompt, char *out, size_t out_size,
                                    char *session_id_out) {
    if (!host_id || !workspace || !prompt || !out || out_size == 0) return false;
    out[0] = '\0';
    if (session_id_out) session_id_out[0] = '\0';

    size_t cmd_size = strlen(workspace) * 8 + strlen(prompt) * 4 + 1024;
    char *cmd = (char *)malloc(cmd_size);
    if (!cmd) return false;

    bool built = mcp_build_prompt_command(host_id, workspace, prompt, cmd, cmd_size);
    if (!built) {
        free(cmd);
        return false;
    }

    char *output = exec_capture(cmd);
    free(cmd);
    if (!output) return false;

    char session_id[MAX_NAME_LEN] = {0};
    mcp_parse_result(host_id, output, out, out_size, session_id);
    free(output);

    if (mcp_id_equals(host_id, "opencode")) {
        mcp_remove_opencode_session(session_id);
    }

    if (out[0] == '\0') return false;
    if (session_id_out) {
        strncpy(session_id_out, session_id, MAX_NAME_LEN - 1);
        session_id_out[MAX_NAME_LEN - 1] = '\0';
    }
    return true;
}

static bool mcp_list_servers_for_host(const char *host_id, const char *workspace,
                                      char *out, size_t out_size) {
    if (!host_id || !out || out_size == 0) return false;
    out[0] = '\0';

    char cmd[MAX_CMD_LEN * 8];
    if (mcp_id_equals(host_id, "agy")) {
        snprintf(cmd, sizeof(cmd), "agy mcp list");
    } else if (mcp_id_equals(host_id, "claude")) {
        snprintf(cmd, sizeof(cmd), "claude mcp list");
    } else if (mcp_id_equals(host_id, "cline")) {
        snprintf(cmd, sizeof(cmd), "cline config mcp --json");
    } else if (mcp_id_equals(host_id, "codex")) {
        snprintf(cmd, sizeof(cmd), "codex mcp list --json");
    } else if (mcp_id_equals(host_id, "opencode")) {
        char param[MAX_PATH_LEN + 64];
        snprintf(param, sizeof(param), "location[directory]=%s",
                 (workspace && workspace[0]) ? workspace : ".");
        char quoted_param[MAX_PATH_LEN * 2 + 128];
        exec_shell_quote(param, quoted_param, sizeof(quoted_param));
        snprintf(cmd, sizeof(cmd), "opencode api mcp.list --param %s", quoted_param);
    } else if (mcp_id_equals(host_id, "openclaw")) {
        snprintf(cmd, sizeof(cmd), "openclaw mcp status --json");
    } else {
        return false;
    }

    char *output = exec_capture(cmd);
    if (!output) return false;
    mcp_parse_servers(host_id, output, out, out_size);
    free(output);
    return out[0] != '\0';
}

typedef struct {
    char path[MAX_PATH_LEN];
    time_t last_active;
} mcp_workspace_t;

static void mcp_sort_workspaces(mcp_workspace_t *items, size_t count) {
    for (size_t i = 0; i + 1 < count; i++) {
        for (size_t j = 0; j + 1 < count - i; j++) {
            if (items[j].last_active < items[j + 1].last_active) {
                mcp_workspace_t tmp = items[j];
                items[j] = items[j + 1];
                items[j + 1] = tmp;
            }
        }
    }
}

static size_t mcp_collect_workspaces(const session_list_t *list,
                                     mcp_workspace_t *workspaces, size_t max_count) {
    size_t count = 0;
    if (list) {
        for (size_t i = 0; i < list->count; i++) {
            const char *ws = list->items[i].workspace;
            if (ws[0] == '\0') continue;

            bool seen = false;
            for (size_t j = 0; j < count; j++) {
                if (strcmp(workspaces[j].path, ws) == 0) {
                    if (list->items[i].last_active > workspaces[j].last_active) {
                        workspaces[j].last_active = list->items[i].last_active;
                    }
                    seen = true;
                    break;
                }
            }
            if (seen) continue;

            if (count < max_count) {
                strncpy(workspaces[count].path, ws, MAX_PATH_LEN - 1);
                workspaces[count].path[MAX_PATH_LEN - 1] = '\0';
                workspaces[count].last_active = list->items[i].last_active;
                count++;
            } else {
                size_t oldest = 0;
                for (size_t j = 1; j < count; j++) {
                    if (workspaces[j].last_active < workspaces[oldest].last_active) {
                        oldest = j;
                    }
                }
                if (list->items[i].last_active > workspaces[oldest].last_active) {
                    strncpy(workspaces[oldest].path, ws, MAX_PATH_LEN - 1);
                    workspaces[oldest].path[MAX_PATH_LEN - 1] = '\0';
                    workspaces[oldest].last_active = list->items[i].last_active;
                }
            }
        }
    }

    if (count == 0) {
        char cwd[MAX_PATH_LEN] = {0};
        if (!getcwd(cwd, sizeof(cwd))) {
            strncpy(cwd, ".", sizeof(cwd) - 1);
        }
        strncpy(workspaces[0].path, cwd, MAX_PATH_LEN - 1);
        workspaces[0].path[MAX_PATH_LEN - 1] = '\0';
        workspaces[0].last_active = time(NULL);
        count = 1;
    }

    mcp_sort_workspaces(workspaces, count);
    return count;
}

static int mcp_preferred_host_index(const flint_config_t *cfg, const session_list_t *list) {
    if (cfg && cfg->mcp_host[0] != '\0' && !mcp_id_equals(cfg->mcp_host, "auto")) {
        return mcp_host_index(cfg->mcp_host);
    }

    int preferred = -1;
    time_t newest = 0;
    if (list) {
        for (size_t i = 0; i < list->count; i++) {
            if (list->items[i].last_active < newest) continue;
            int host = mcp_host_index(list->items[i].tool_cmd);
            if (host >= 0) {
                newest = list->items[i].last_active;
                preferred = host;
            }
        }
    }
    return preferred;
}

static bool mcp_fill_target(mcp_target_t *out, const mcp_host_info_t *host,
                            const char *servers, const char *workspace) {
    if (!out || !host || !servers || !workspace) return false;

    strncpy(out->host_id, host->id, sizeof(out->host_id) - 1);
    strncpy(out->host_name, host->name, sizeof(out->host_name) - 1);
    strncpy(out->servers, servers, sizeof(out->servers) - 1);
    strncpy(out->workspace, workspace, sizeof(out->workspace) - 1);
    strncpy(out->server_name, servers, sizeof(out->server_name) - 1);
    out->host_id[sizeof(out->host_id) - 1] = '\0';
    out->host_name[sizeof(out->host_name) - 1] = '\0';
    out->servers[sizeof(out->servers) - 1] = '\0';
    out->workspace[sizeof(out->workspace) - 1] = '\0';
    out->server_name[sizeof(out->server_name) - 1] = '\0';

    char *comma = strchr(out->server_name, ',');
    if (comma) *comma = '\0';
    return true;
}

bool mcp_detect_connected(const flint_config_t *cfg, const session_list_t *list,
                          mcp_target_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    bool pinned = cfg && cfg->mcp_host[0] != '\0' && !mcp_id_equals(cfg->mcp_host, "auto");
    int preferred = mcp_preferred_host_index(cfg, list);
    if (pinned && preferred < 0) return false;

    mcp_workspace_t workspaces[MCP_MAX_WORKSPACES];
    size_t workspace_count = mcp_collect_workspaces(list, workspaces, MCP_MAX_WORKSPACES);

    int order[MCP_HOST_COUNT];
    size_t order_count = 0;
    if (preferred >= 0) order[order_count++] = preferred;
    if (!pinned) {
        for (size_t i = 0; i < MCP_HOST_COUNT; i++) {
            if ((int)i == preferred) continue;
            order[order_count++] = (int)i;
        }
    }

    for (size_t i = 0; i < order_count; i++) {
        const mcp_host_info_t *host = &MCP_HOSTS[order[i]];
        if (!exec_command_exists(host->binary)) continue;

        if (!pinned &&
            !mcp_host_may_have_servers(host->id, workspaces[0].path)) {
            continue;
        }

        if (mcp_host_needs_workspace(host->id)) {
            for (size_t w = 0; w < workspace_count; w++) {
                char servers[512] = {0};
                if (mcp_list_servers_for_host(host->id, workspaces[w].path,
                                              servers, sizeof(servers))) {
                    return mcp_fill_target(out, host, servers, workspaces[w].path);
                }
            }
        } else {
            char servers[512] = {0};
            if (mcp_list_servers_for_host(host->id, workspaces[0].path,
                                          servers, sizeof(servers))) {
                return mcp_fill_target(out, host, servers, workspaces[0].path);
            }
        }
    }

    return false;
}

static const char *const MCP_FOCUS_INSTRUCTIONS =
    "You are preparing a short morning focus briefing. You may use only read-only tools and queries.\n\n"
    "Recent work context from the user's last working day:\n%s\n"
    "Connected tools available: %s.\n\n"
    "Use read-only searches in those tools (for example messaging platforms such as Slack or "
    "Microsoft Teams, recent issues, or documents) to find what genuinely needs attention today.\n"
    "Reply with only 3 to 5 terse bullet points, each starting with \"- \", describing concrete "
    "priorities for today. Do not write, send, or change anything. Do not explain your process.";

bool mcp_query_focus(const flint_config_t *cfg, const mcp_target_t *target,
                     const char *focus_context, char *out, size_t out_size) {
    if (!cfg || !cfg->mcp_focus || !target || !out || out_size == 0) return false;
    out[0] = '\0';

    if (target->host_id[0] == '\0' ||
        target->servers[0] == '\0' ||
        target->workspace[0] == '\0') {
        return false;
    }

    const char *context = (focus_context && focus_context[0] != '\0')
                        ? focus_context
                        : "- No local session context available.\n";

    size_t prompt_size = strlen(context) + strlen(MCP_FOCUS_INSTRUCTIONS) + 512;
    char *prompt = (char *)malloc(prompt_size);
    if (!prompt) return false;

    snprintf(prompt, prompt_size, MCP_FOCUS_INSTRUCTIONS, context, target->servers);
    bool ok = mcp_run_prompt_for_host(target->host_id, target->workspace, prompt,
                                      out, out_size, NULL);
    free(prompt);
    return ok;
}

bool mcp_query_prompt(const char *host_id, const char *workspace, const char *context,
                      const char *servers, const char *instructions, char *out,
                      size_t out_size, char *session_id_out) {
    if (!host_id || !workspace || !context || !instructions || !out || out_size == 0) {
        return false;
    }

    size_t prompt_size = strlen(context) + strlen(instructions) + 512;
    char *prompt = (char *)malloc(prompt_size);
    if (!prompt) return false;

    snprintf(prompt, prompt_size, instructions, context, servers ? servers : "none");
    bool ok = mcp_run_prompt_for_host(host_id, workspace, prompt, out, out_size, session_id_out);
    free(prompt);
    return ok;
}

#ifdef TEST_MODE
bool flint_mcp_parse_servers(const char *host_id, const char *output,
                             char *out, size_t out_size) {
    if (!host_id || !output || !out || out_size == 0) return false;
    mcp_parse_servers(host_id, output, out, out_size);
    return out[0] != '\0';
}

bool flint_mcp_parse_result(const char *host_id, const char *output,
                            char *out, size_t out_size, char *session_id_out) {
    if (!host_id || !output || !out || out_size == 0) return false;
    mcp_parse_result(host_id, output, out, out_size, session_id_out);
    return out[0] != '\0';
}

bool flint_mcp_build_prompt_command(const char *host_id, const char *workspace,
                                    const char *prompt, char *out, size_t out_size) {
    return mcp_build_prompt_command(host_id, workspace, prompt, out, out_size);
}

bool flint_mcp_host_may_have_servers(const char *host_id, const char *workspace) {
    return mcp_host_may_have_servers(host_id, workspace);
}

const char *flint_mcp_preferred_host(const flint_config_t *cfg, const session_list_t *list) {
    int host = mcp_preferred_host_index(cfg, list);
    return (host >= 0) ? MCP_HOSTS[host].id : "";
}
#endif
