#include "ai_adapter.h"
#include "../util/fs.h"
#include "../util/json.h"
#include "../config/config.h"
#include "../git/git.h"
#include "../process/proc.h"
#include <string.h>
#include <ctype.h>

#define CODEX_HEAD_BYTES (128 * 1024)
#define CODEX_MAX_DEPTH 6
#define CODEX_MAX_FILES 2000

static void codex_home_dir(char *out, size_t out_size) {
    const char *env_home = getenv("CODEX_HOME");
    if (env_home && env_home[0] != '\0') {
        strncpy(out, env_home, out_size - 1);
        out[out_size - 1] = '\0';
    } else {
        fs_expand_path("~/.codex", out, out_size);
    }
}

static bool codex_is_available(void) {
    char codex_dir[MAX_PATH_LEN];
    codex_home_dir(codex_dir, sizeof(codex_dir));
    return fs_dir_exists(codex_dir);
}

static bool codex_is_meta_prompt(const char *text) {
    static const char *prefixes[] = {
        "<environment_context>", "<user_instructions>", "<INSTRUCTIONS>",
        "<skills_instructions>", "<plugins_instructions>", "<tools>"
    };
    if (!text || text[0] != '<') return false;
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        if (str_has_prefix(text, prefixes[i])) return true;
    }
    return false;
}

static bool codex_meta_string(const char *meta, const char *key, char *out, size_t out_size) {
    if (!json_get_string(meta, key, out, out_size)) return false;
    return out[0] != '\0';
}

typedef struct {
    codex_head_t *out;
    bool found_meta;
} codex_parse_ctx_t;

static void codex_read_meta(const char *meta, codex_parse_ctx_t *ctx) {
    codex_head_t *out = ctx->out;

    if (out->session_id[0] == '\0') {
        if (!codex_meta_string(meta, "id", out->session_id, sizeof(out->session_id))) {
            codex_meta_string(meta, "session_id", out->session_id, sizeof(out->session_id));
        }
    }
    if (out->workspace[0] == '\0') {
        json_get_string(meta, "cwd", out->workspace, sizeof(out->workspace));
    }
    if (out->git_branch[0] == '\0') {
        char git[2048] = {0};
        if (json_get_block(meta, "git", git, sizeof(git))) {
            json_get_string(git, "branch", out->git_branch, sizeof(out->git_branch));
        }
    }

    char agent_value[MAX_NAME_LEN] = {0};
    if (codex_meta_string(meta, "parent_thread_id", agent_value, sizeof(agent_value)) ||
        codex_meta_string(meta, "agent_role", agent_value, sizeof(agent_value)) ||
        codex_meta_string(meta, "agent_nickname", agent_value, sizeof(agent_value))) {
        out->is_agent = true;
    } else if (codex_meta_string(meta, "thread_source", agent_value, sizeof(agent_value)) &&
               (str_contains_ignore_case(agent_value, "agent") ||
                str_contains_ignore_case(agent_value, "subagent"))) {
        out->is_agent = true;
    }

    ctx->found_meta = true;
}

static int codex_head_line(const char *line, size_t len, size_t line_number, void *userdata) {
    (void)len;
    (void)line_number;
    codex_parse_ctx_t *ctx = (codex_parse_ctx_t *)userdata;
    codex_head_t *out = ctx->out;

    char type[64] = {0};
    bool has_type = json_get_string(line, "type", type, sizeof(type));

    if (!ctx->found_meta) {
        if (has_type && strcmp(type, "session_meta") == 0) {
            char payload[16384] = {0};
            if (json_get_block(line, "payload", payload, sizeof(payload))) {
                codex_read_meta(payload, ctx);
            } else {
                codex_read_meta(line, ctx);
            }
        } else {
            /* Old rollouts wrote the session metadata as the first bare object. */
            char id[MAX_NAME_LEN] = {0};
            char cwd[MAX_PATH_LEN] = {0};
            if (json_get_string(line, "id", id, sizeof(id)) &&
                json_get_string(line, "cwd", cwd, sizeof(cwd))) {
                codex_read_meta(line, ctx);
            }
        }
    }

    if (out->summary[0] != '\0') return 0;

    /* Legacy rollouts persist a user_message event. */
    if (has_type && strcmp(type, "event_msg") == 0) {
        char payload[16384] = {0};
        if (json_get_block(line, "payload", payload, sizeof(payload))) {
            char payload_type[64] = {0};
            json_get_string(payload, "type", payload_type, sizeof(payload_type));
            if (strcmp(payload_type, "user_message") == 0 ||
                strcmp(payload_type, "item_completed") == 0) {
                if (!json_get_string(payload, "message", out->summary, sizeof(out->summary))) {
                    json_get_string(payload, "text", out->summary, sizeof(out->summary));
                }
            }
        }
    }

    /* Newer rollouts persist the first user turn as a response_item message. */
    if (out->summary[0] == '\0' && strstr(line, "\"role\":\"user\"") != NULL) {
        json_get_string(line, "text", out->summary, sizeof(out->summary));
    }

    if (codex_is_meta_prompt(out->summary)) {
        out->summary[0] = '\0';
    }

    return 0;
}

static bool codex_uuid_at(const char *s, size_t len) {
    if (!s || len != 36) return false;
    for (size_t i = 0; i < len; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return false;
        } else if (!isxdigit((unsigned char)s[i])) {
            return false;
        }
    }
    return true;
}

static void codex_filename_id(const char *name, char *out, size_t out_size) {
    if (!name || !out || out_size == 0) return;
    out[0] = '\0';

    size_t len = strlen(name);
    const char *suffix = ".jsonl";
    size_t suffix_len = strlen(suffix);
    if (len >= suffix_len && strcmp(name + len - suffix_len, suffix) == 0) {
        len -= suffix_len;
    }

    if (len >= 36) {
        const char *candidate = name + len - 36;
        if (codex_uuid_at(candidate, 36)) {
            strncpy(out, candidate, out_size - 1);
            out[out_size - 1] = '\0';
        }
    }
}

bool flint_codex_parse_head(const char *jsonl, codex_head_t *out) {
    if (!jsonl || !out) return false;
    memset(out, 0, sizeof(*out));

    codex_parse_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.out = out;

    jsonl_for_each_line(jsonl, codex_head_line, &ctx);

    str_collapse_whitespace(out->summary);
    str_trim(out->summary);

    return out->session_id[0] != '\0' || out->workspace[0] != '\0';
}

typedef struct {
    const flint_config_t *cfg;
    int lookback_hours;
    time_t cutoff_time;
    session_list_t *list;
    int depth;
    int files_scanned;
} codex_scan_t;

static int codex_dir_cb(const char *parent, const char *entry_name, bool is_dir, void *userdata);

static bool codex_add_rollout(const char *path, const char *name, codex_scan_t *scan) {
    if (scan->files_scanned >= CODEX_MAX_FILES) return false;
    scan->files_scanned++;

    time_t mtime = fs_get_mod_time(path);
    if (mtime < scan->cutoff_time) return true;

    size_t size = 0;
    char *content = fs_read_head(path, CODEX_HEAD_BYTES, &size);
    if (!content) return true;

    codex_head_t head;
    bool parsed = flint_codex_parse_head(content, &head);
    free(content);

    if (!parsed) return true;

    if (head.session_id[0] == '\0') {
        codex_filename_id(name, head.session_id, sizeof(head.session_id));
    }
    if (head.session_id[0] == '\0') return true;

    if (head.workspace[0] == '\0') {
        fs_expand_path("~", head.workspace, sizeof(head.workspace));
    }

    session_t session;
    memset(&session, 0, sizeof(session));
    strncpy(session.id, head.session_id, sizeof(session.id) - 1);
    strncpy(session.tool_name, "ChatGPT Codex", sizeof(session.tool_name) - 1);
    strncpy(session.tool_cmd, "codex", sizeof(session.tool_cmd) - 1);
    session.origin = head.is_agent ? SESSION_ORIGIN_AGENT : SESSION_ORIGIN_USER;

    strncpy(session.workspace, head.workspace, sizeof(session.workspace) - 1);
    session.last_active = (mtime > 0) ? mtime : time(NULL);
    fs_format_time(session.last_active, session.last_active_str, sizeof(session.last_active_str));

    if (head.summary[0] != '\0') {
        strncpy(session.summary, head.summary, sizeof(session.summary) - 1);
    } else {
        snprintf(session.summary, sizeof(session.summary), "Codex CLI session");
    }
    session.was_merely_open = false;

    config_format_tool_cmd(scan->cfg, "codex", session.id,
                           session.resume_cmd, sizeof(session.resume_cmd));

    git_inspect_workspace(session.workspace, scan->lookback_hours, &session.git);
    session.is_already_running = proc_is_session_running("codex", session.workspace);

    session_list_add(scan->list, &session);
    return true;
}

static int codex_dir_cb(const char *parent, const char *entry_name, bool is_dir, void *userdata) {
    codex_scan_t *scan = (codex_scan_t *)userdata;
    if (scan->files_scanned >= CODEX_MAX_FILES) return 1;

    char full_path[MAX_PATH_LEN];
    fs_path_join(parent, entry_name, full_path, sizeof(full_path));

    if (is_dir) {
        if (scan->depth >= CODEX_MAX_DEPTH) return 0;
        scan->depth++;
        fs_list_dir(full_path, codex_dir_cb, scan);
        scan->depth--;
        return 0;
    }

    if (!str_has_suffix(entry_name, ".jsonl")) return 0;
    codex_add_rollout(full_path, entry_name, scan);
    return 0;
}

static int codex_discover(const flint_config_t *cfg, int lookback_hours, session_list_t *list) {
    char codex_dir[MAX_PATH_LEN];
    char sessions_dir[MAX_PATH_LEN];
    codex_home_dir(codex_dir, sizeof(codex_dir));
    fs_path_join(codex_dir, "sessions", sessions_dir, sizeof(sessions_dir));

    time_t now = time(NULL);
    time_t cutoff = now - (lookback_hours * 3600);

    if (fs_dir_exists(sessions_dir)) {
        codex_scan_t scan;
        memset(&scan, 0, sizeof(scan));
        scan.cfg = cfg;
        scan.lookback_hours = lookback_hours;
        scan.cutoff_time = cutoff;
        scan.list = list;
        fs_list_dir(sessions_dir, codex_dir_cb, &scan);
    }

    return 0;
}

const ai_adapter_t codex_adapter = {
    .name = "ChatGPT Codex",
    .cli_binary = "codex",
    .is_available = codex_is_available,
    .discover_sessions = codex_discover
};

static const ai_adapter_t *all_adapters[] = {
    &antigravity_adapter,
    &claude_adapter,
    &codex_adapter,
    &cline_adapter,
    &opencode_adapter,
    &openclaw_adapter
};

size_t ai_get_adapters(const ai_adapter_t ***adapters) {
    if (adapters) {
        *adapters = all_adapters;
    }
    return sizeof(all_adapters) / sizeof(all_adapters[0]);
}
