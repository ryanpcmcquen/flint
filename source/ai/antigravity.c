#include "ai_adapter.h"
#include "../util/fs.h"
#include "../util/json.h"
#include "../config/config.h"
#include "../git/git.h"
#include "../process/proc.h"
#include <string.h>

typedef struct {
    char session_id[MAX_NAME_LEN];
    char workspace[MAX_PATH_LEN];
    char last_user_req[MAX_SUMMARY_LEN];
    char accomplishments[MAX_SUMMARY_LEN];
    time_t last_time;
    int prompt_count;
    session_origin_t origin;
} agy_scan_ctx_t;

static int parse_transcript_line(const char *line, size_t len, size_t line_num, void *userdata) {
    (void)line_num;
    agy_scan_ctx_t *ctx = (agy_scan_ctx_t *)userdata;

    char *buf = (char *)malloc(len + 1);
    if (!buf) return 0;
    strncpy(buf, line, len);
    buf[len] = '\0';

    char source[64] = {0};
    if (json_get_string(buf, "source", source, sizeof(source))) {
        if (strcasecmp(source, "USER_EXPLICIT") == 0) {
            ctx->origin = SESSION_ORIGIN_USER;
        }
    }

    char type[64] = {0};
    if (json_get_string(buf, "type", type, sizeof(type))) {
        if (strcmp(type, "USER_INPUT") == 0) {
            char content[1024] = {0};
            if (json_get_string(buf, "content", content, sizeof(content))) {
                char req[512] = {0};
                extract_tagged_text(content, "USER_REQUEST", req, sizeof(req));
                str_trim(req);
                if (req[0] != '\0') {
                    strncpy(ctx->last_user_req, req, sizeof(ctx->last_user_req) - 1);
                    ctx->prompt_count++;
                }

                /* Extract workspace if present in Active Workspaces or Command Working Directory.
                 * Antigravity CLI may record either tag depending on the transcript format. */
                const char *ws_tag = "Active Workspaces:";
                const char *cwd_tag = "Command Working Directory:";
                const char *ws_found = strstr(content, ws_tag);
                if (!ws_found) {
                    ws_found = strstr(content, cwd_tag);
                    if (ws_found) {
                        ws_tag = cwd_tag;
                    }
                }
                if (ws_found) {
                    const char *line_start = ws_found + strlen(ws_tag);
                    while (*line_start && (*line_start == ' ' || *line_start == '\n' || *line_start == '\r' || *line_start == '-')) {
                        line_start++;
                    }
                    const char *line_end = line_start;
                    while (*line_end && *line_end != '\n' && *line_end != '\r') line_end++;
                    size_t ws_len = (size_t)(line_end - line_start);
                    if (ws_len > 0 && ws_len < sizeof(ctx->workspace)) {
                        strncpy(ctx->workspace, line_start, ws_len);
                        ctx->workspace[ws_len] = '\0';
                        str_trim(ctx->workspace);
                    }
                }
            }
        } else if (strcmp(type, "PLANNER_RESPONSE") == 0) {
            char tool_summary[256] = {0};
            if (json_get_string(buf, "toolSummary", tool_summary, sizeof(tool_summary))) {
                str_trim(tool_summary);
                if (tool_summary[0] != '\0' && !strstr(ctx->accomplishments, tool_summary)) {
                    if (ctx->accomplishments[0] != '\0') {
                        strncat(ctx->accomplishments, "; ", sizeof(ctx->accomplishments) - strlen(ctx->accomplishments) - 1);
                    }
                    strncat(ctx->accomplishments, tool_summary, sizeof(ctx->accomplishments) - strlen(ctx->accomplishments) - 1);
                }
            }
        }
    }

    free(buf);
    return 0;
}

typedef struct {
    const flint_config_t *cfg;
    int lookback_hours;
    time_t cutoff_time;
    session_list_t *list;
} agy_dir_walker_t;

static int agy_dir_callback(const char *parent, const char *entry_name, bool is_dir, void *userdata) {
    if (!is_dir) return 0;
    agy_dir_walker_t *walker = (agy_dir_walker_t *)userdata;

    char transcript_path[MAX_PATH_LEN];
    snprintf(transcript_path, sizeof(transcript_path), "%s%s%s%s.system_generated%slogs%stranscript.jsonl",
             parent, PATH_SEP_STR, entry_name, PATH_SEP_STR, PATH_SEP_STR, PATH_SEP_STR);

    if (!fs_file_exists(transcript_path)) {
        return 0;
    }

    time_t mtime = fs_get_mod_time(transcript_path);
    if (mtime < walker->cutoff_time) {
        return 0;
    }

    agy_scan_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    strncpy(ctx.session_id, entry_name, sizeof(ctx.session_id) - 1);
    ctx.last_time = mtime;
    ctx.origin = SESSION_ORIGIN_USER;

    /* Read head of transcript for workspace metadata */
    char expanded_path[1024];
    fs_expand_path(transcript_path, expanded_path, sizeof(expanded_path));
    FILE *f = fopen(expanded_path, "rb");
    if (f) {
        char head_buf[8192];
        size_t r = fread(head_buf, 1, sizeof(head_buf) - 1, f);
        head_buf[r] = '\0';
        fclose(f);

        if (strstr(head_buf, "\"source\": \"USER_EXPLICIT\"") ||
            strstr(head_buf, "\"source\":\"USER_EXPLICIT\"")) {
            ctx.origin = SESSION_ORIGIN_USER;
        } else if (strstr(head_buf, "\"source\": \"AGENT\"") ||
                   strstr(head_buf, "\"source\":\"AGENT\"") ||
                   strstr(head_buf, "\"parent_conversation_id\"")) {
            ctx.origin = SESSION_ORIGIN_AGENT;
        } else {
            ctx.origin = SESSION_ORIGIN_USER;
        }

        const char *ws_tag = "Active Workspaces:";
        const char *cwd_tag = "Command Working Directory:";
        const char *found = strstr(head_buf, ws_tag);
        if (!found) found = strstr(head_buf, cwd_tag);

        if (found) {
            const char *p = found + (found == strstr(head_buf, ws_tag) ? strlen(ws_tag) : strlen(cwd_tag));
            while (*p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '-')) p++;
            const char *end = p;
            while (*end && *end != '\n' && *end != '\r') end++;
            size_t len = (size_t)(end - p);
            if (len > 0 && len < sizeof(ctx.workspace)) {
                strncpy(ctx.workspace, p, len);
                ctx.workspace[len] = '\0';
                str_trim(ctx.workspace);
            }
        }
    }

    size_t size = 0;
    char *tail_content = fs_read_tail(transcript_path, 64 * 1024, &size);
    if (tail_content) {
        jsonl_for_each_line(tail_content, parse_transcript_line, &ctx);
        free(tail_content);
    }

    session_t session;
    memset(&session, 0, sizeof(session));
    strncpy(session.id, ctx.session_id, sizeof(session.id) - 1);
    strncpy(session.tool_name, "Antigravity CLI", sizeof(session.tool_name) - 1);
    strncpy(session.tool_cmd, "agy", sizeof(session.tool_cmd) - 1);
    session.origin = ctx.origin;

    if (ctx.workspace[0] != '\0') {
        strncpy(session.workspace, ctx.workspace, sizeof(session.workspace) - 1);
    } else {
        fs_expand_path("~", session.workspace, sizeof(session.workspace));
    }

    session.last_active = ctx.last_time;
    fs_format_time(ctx.last_time, session.last_active_str, sizeof(session.last_active_str));

    if (ctx.last_user_req[0] != '\0') {
        char req_snippet[256];
        str_truncate_clean(req_snippet, ctx.last_user_req, 120);
        if (ctx.accomplishments[0] != '\0') {
            char acc_snippet[256];
            str_truncate_clean(acc_snippet, ctx.accomplishments, 120);
            snprintf(session.summary, sizeof(session.summary), "%s (%s)", req_snippet, acc_snippet);
        } else {
            snprintf(session.summary, sizeof(session.summary), "%s", req_snippet);
        }
        session.was_merely_open = false;
    } else if (ctx.accomplishments[0] != '\0') {
        str_truncate_clean(session.summary, ctx.accomplishments, sizeof(session.summary) - 1);
        session.was_merely_open = false;
    } else {
        /* Session was merely open yesterday without completed prompts */
        session.was_merely_open = true;
        snprintf(session.summary, sizeof(session.summary), "Session open (idle / no completed prompts yesterday)");
    }

    /* Format resume command using config wrapper, model, and effort */
    config_format_tool_cmd(walker->cfg, "agy", session.id, session.resume_cmd, sizeof(session.resume_cmd));

    /* Inspect Git status for workspace */
    git_inspect_workspace(session.workspace, walker->lookback_hours, &session.git);

    /* Check if already running */
    session.is_already_running = proc_is_session_running("agy", session.workspace);

    session_list_add(walker->list, &session);
    return 0;
}

static bool antigravity_is_available(void) {
    char brain_dir[MAX_PATH_LEN];
    fs_expand_path("~/.gemini/antigravity-cli/brain", brain_dir, sizeof(brain_dir));
    return fs_dir_exists(brain_dir);
}

static int antigravity_discover(const flint_config_t *cfg, int lookback_hours, session_list_t *list) {
    char brain_dir[MAX_PATH_LEN];
    fs_expand_path("~/.gemini/antigravity-cli/brain", brain_dir, sizeof(brain_dir));
    if (!fs_dir_exists(brain_dir)) {
        return 0;
    }

    time_t now = time(NULL);
    time_t cutoff = now - (lookback_hours * 3600);

    agy_dir_walker_t walker;
    walker.cfg = cfg;
    walker.lookback_hours = lookback_hours;
    walker.cutoff_time = cutoff;
    walker.list = list;

    fs_list_dir(brain_dir, agy_dir_callback, &walker);
    return 0;
}

const ai_adapter_t antigravity_adapter = {
    .name = "Antigravity CLI",
    .cli_binary = "agy",
    .is_available = antigravity_is_available,
    .discover_sessions = antigravity_discover
};
