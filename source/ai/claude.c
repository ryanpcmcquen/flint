#include "ai_adapter.h"
#include "../util/fs.h"
#include "../util/json.h"
#include "../config/config.h"
#include "../git/git.h"
#include "../process/proc.h"
#include <string.h>

#define CLAUDE_HEAD_BYTES (128 * 1024)
#define CLAUDE_MAX_DEPTH 4
#define CLAUDE_MAX_FILES 2000

static bool claude_is_available(void) {
    char claude_json[MAX_PATH_LEN];
    char claude_dir[MAX_PATH_LEN];
    fs_expand_path("~/.claude.json", claude_json, sizeof(claude_json));
    fs_expand_path("~/.claude", claude_dir, sizeof(claude_dir));
    return fs_file_exists(claude_json) || fs_dir_exists(claude_dir);
}

/* Claude injects synthetic messages for slash commands and context. They are
 * not useful as a human-readable session summary. */
static bool claude_is_meta_prompt(const char *text) {
    static const char *prefixes[] = {
        "<command-", "<local-command", "<environment_context>",
        "<user_instructions>", "<system-reminder>", "<INSTRUCTIONS>"
    };
    if (!text || text[0] != '<') return false;
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        if (str_has_prefix(text, prefixes[i])) return true;
    }
    return false;
}

typedef struct {
    claude_head_t *out;
    char title[MAX_SUMMARY_LEN];
    char fallback_summary[MAX_SUMMARY_LEN];
} claude_parse_ctx_t;

static int claude_head_line(const char *line, size_t len, size_t line_number, void *userdata) {
    (void)len;
    (void)line_number;
    claude_parse_ctx_t *ctx = (claude_parse_ctx_t *)userdata;
    claude_head_t *out = ctx->out;

    if (out->session_id[0] == '\0') {
        json_get_string(line, "sessionId", out->session_id, sizeof(out->session_id));
    }
    if (out->workspace[0] == '\0') {
        json_get_string(line, "cwd", out->workspace, sizeof(out->workspace));
    }

    bool sidechain = false;
    if (json_get_bool(line, "isSidechain", &sidechain) && sidechain) {
        out->is_sidechain = true;
    }

    char type[64] = {0};
    if (!json_get_string(line, "type", type, sizeof(type))) return 0;

    if (strcmp(type, "custom-title") == 0) {
        json_get_string(line, "customTitle", ctx->title, sizeof(ctx->title));
        return 0;
    }

    /* Generated conversation summaries are a decent fallback when no user
     * prompt is present in the head of the transcript. */
    if (strcmp(type, "summary") == 0) {
        if (ctx->fallback_summary[0] == '\0') {
            json_get_string(line, "summary", ctx->fallback_summary, sizeof(ctx->fallback_summary));
        }
        return 0;
    }

    if (strcmp(type, "user") == 0 && out->is_sidechain == false && out->summary[0] == '\0') {
        char message[8192] = {0};
        if (json_get_block(line, "message", message, sizeof(message))) {
            /* message.content is either a string or an array of blocks. */
            if (!json_get_string(message, "content", out->summary, sizeof(out->summary))) {
                json_get_string(message, "text", out->summary, sizeof(out->summary));
            }
        }
        if (out->summary[0] == '\0') {
            json_get_string(line, "text", out->summary, sizeof(out->summary));
        }
        if (claude_is_meta_prompt(out->summary)) {
            out->summary[0] = '\0';
        }
    }

    return 0;
}

bool flint_claude_parse_head(const char *jsonl, claude_head_t *out) {
    if (!jsonl || !out) return false;
    memset(out, 0, sizeof(*out));

    claude_parse_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.out = out;

    jsonl_for_each_line(jsonl, claude_head_line, &ctx);

    if (ctx.title[0] != '\0') {
        strncpy(out->summary, ctx.title, sizeof(out->summary) - 1);
    } else if (out->summary[0] == '\0' && ctx.fallback_summary[0] != '\0') {
        strncpy(out->summary, ctx.fallback_summary, sizeof(out->summary) - 1);
    }
    str_collapse_whitespace(out->summary);
    str_trim(out->summary);

    return out->session_id[0] != '\0' || out->workspace[0] != '\0';
}

static void claude_filename_id(const char *name, char *out, size_t out_size) {
    if (!name || !out || out_size == 0) return;
    strncpy(out, name, out_size - 1);
    out[out_size - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot && strcmp(dot, ".jsonl") == 0) *dot = '\0';
}

typedef struct {
    const flint_config_t *cfg;
    int lookback_hours;
    time_t cutoff_time;
    session_list_t *list;
    int depth;
    int files_scanned;
} claude_scan_t;

static int claude_dir_cb(const char *parent, const char *entry_name, bool is_dir, void *userdata);

static bool claude_add_transcript(const char *path, const char *name, claude_scan_t *scan) {
    if (scan->files_scanned >= CLAUDE_MAX_FILES) return false;
    scan->files_scanned++;

    time_t mtime = fs_get_mod_time(path);
    if (mtime < scan->cutoff_time) return true;

    size_t size = 0;
    char *content = fs_read_head(path, CLAUDE_HEAD_BYTES, &size);
    if (!content) return true;

    claude_head_t head;
    bool parsed = flint_claude_parse_head(content, &head);
    free(content);

    if (!parsed) return true;

    if (head.session_id[0] == '\0') {
        claude_filename_id(name, head.session_id, sizeof(head.session_id));
    }
    if (head.session_id[0] == '\0') return true;

    if (head.workspace[0] == '\0') {
        fs_expand_path("~", head.workspace, sizeof(head.workspace));
    }

    session_t session;
    memset(&session, 0, sizeof(session));
    strncpy(session.id, head.session_id, sizeof(session.id) - 1);
    strncpy(session.tool_name, "Claude Code", sizeof(session.tool_name) - 1);
    strncpy(session.tool_cmd, "claude", sizeof(session.tool_cmd) - 1);

    session.origin = (head.is_sidechain || strstr(name, "agent-") != NULL)
                         ? SESSION_ORIGIN_AGENT
                         : SESSION_ORIGIN_USER;

    strncpy(session.workspace, head.workspace, sizeof(session.workspace) - 1);
    session.last_active = (mtime > 0) ? mtime : time(NULL);
    fs_format_time(session.last_active, session.last_active_str, sizeof(session.last_active_str));

    if (head.summary[0] != '\0') {
        strncpy(session.summary, head.summary, sizeof(session.summary) - 1);
    } else {
        snprintf(session.summary, sizeof(session.summary), "Claude Code session");
    }
    session.was_merely_open = false;

    config_format_tool_cmd(scan->cfg, "claude", session.id,
                           session.resume_cmd, sizeof(session.resume_cmd));

    git_inspect_workspace(session.workspace, scan->lookback_hours, &session.git);
    session.is_already_running = proc_is_session_running("claude", session.workspace);

    session_list_add(scan->list, &session);
    return true;
}

static int claude_dir_cb(const char *parent, const char *entry_name, bool is_dir, void *userdata) {
    claude_scan_t *scan = (claude_scan_t *)userdata;
    if (scan->files_scanned >= CLAUDE_MAX_FILES) return 1;

    char full_path[MAX_PATH_LEN];
    fs_path_join(parent, entry_name, full_path, sizeof(full_path));

    if (is_dir) {
        if (scan->depth >= CLAUDE_MAX_DEPTH) return 0;
        scan->depth++;
        fs_list_dir(full_path, claude_dir_cb, scan);
        scan->depth--;
        return 0;
    }

    if (!str_has_suffix(entry_name, ".jsonl")) return 0;
    claude_add_transcript(full_path, entry_name, scan);
    return 0;
}

static int claude_discover(const flint_config_t *cfg, int lookback_hours, session_list_t *list) {
    char projects_dir[MAX_PATH_LEN];
    fs_expand_path("~/.claude/projects", projects_dir, sizeof(projects_dir));

    time_t now = time(NULL);
    time_t cutoff = now - (lookback_hours * 3600);

    if (fs_dir_exists(projects_dir)) {
        claude_scan_t scan;
        memset(&scan, 0, sizeof(scan));
        scan.cfg = cfg;
        scan.lookback_hours = lookback_hours;
        scan.cutoff_time = cutoff;
        scan.list = list;
        fs_list_dir(projects_dir, claude_dir_cb, &scan);
    }

    return 0;
}

const ai_adapter_t claude_adapter = {
    .name = "Claude Code",
    .cli_binary = "claude",
    .is_available = claude_is_available,
    .discover_sessions = claude_discover
};
