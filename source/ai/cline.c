#include "ai_adapter.h"
#include "../util/fs.h"
#include "../util/json.h"
#include "../config/config.h"
#include "../git/git.h"
#include "../process/proc.h"
#include <string.h>

typedef struct {
    const flint_config_t *cfg;
    int lookback_hours;
    time_t cutoff_time;
    session_list_t *list;
} cline_scan_t;

/* Cline stores prompts as <user_input mode="act">...</user_input>. */
static void cline_clean_prompt(const char *prompt, char *out, size_t out_size) {
    if (!prompt || !out || out_size == 0) return;

    const char *start = prompt;
    const char *tag = strstr(prompt, "<user_input");
    if (tag) {
        const char *end_tag = strchr(tag, '>');
        if (end_tag) start = end_tag + 1;
    }

    const char *end = strstr(start, "</user_input>");
    size_t len = end ? (size_t)(end - start) : strlen(start);
    if (len >= out_size) len = out_size - 1;

    memcpy(out, start, len);
    out[len] = '\0';
    str_trim(out);
}

static int cline_session_dir_cb(const char *parent, const char *entry_name, bool is_dir, void *userdata) {
    if (!is_dir) return 0;
    cline_scan_t *scan = (cline_scan_t *)userdata;

    char session_path[MAX_PATH_LEN];
    snprintf(session_path, sizeof(session_path), "%s%c%s%c%s.json",
             parent, PATH_SEP, entry_name, PATH_SEP, entry_name);

    if (!fs_file_exists(session_path)) {
        return 0;
    }

    time_t mtime = fs_get_mod_time(session_path);
    if (mtime < scan->cutoff_time) {
        return 0;
    }

    size_t size = 0;
    char *content = fs_read_file(session_path, &size);
    if (!content) {
        return 0;
    }

    session_t session;
    memset(&session, 0, sizeof(session));

    if (!json_get_string(content, "session_id", session.id, sizeof(session.id))) {
        strncpy(session.id, entry_name, sizeof(session.id) - 1);
    }

    strncpy(session.tool_name, "Cline", sizeof(session.tool_name) - 1);
    strncpy(session.tool_cmd, "cline", sizeof(session.tool_cmd) - 1);

    char workspace[MAX_PATH_LEN] = {0};
    if (!json_get_string(content, "workspace_root", workspace, sizeof(workspace)) ||
        workspace[0] == '\0') {
        json_get_string(content, "cwd", workspace, sizeof(workspace));
    }
    if (workspace[0] != '\0') {
        strncpy(session.workspace, workspace, sizeof(session.workspace) - 1);
    } else {
        fs_expand_path("~", session.workspace, sizeof(session.workspace));
    }

    char origin_block[512] = {0};
    char origin_mode[64] = {0};
    if (json_get_block(content, "sessionHistoryOrigin", origin_block, sizeof(origin_block))) {
        json_get_string(origin_block, "mode", origin_mode, sizeof(origin_mode));
    }
    session.origin = (strcmp(origin_mode, "agent") == 0 ||
                      strstr(entry_name, "subagent") != NULL) ?
                     SESSION_ORIGIN_AGENT : SESSION_ORIGIN_USER;

    char title[MAX_SUMMARY_LEN] = {0};
    json_get_string(content, "title", title, sizeof(title));

    if (title[0] != '\0') {
        str_truncate_clean(session.summary, title, sizeof(session.summary) - 1);
        session.was_merely_open = false;
    } else {
        char prompt[MAX_SUMMARY_LEN] = {0};
        char cleaned[MAX_SUMMARY_LEN] = {0};
        json_get_string(content, "prompt", prompt, sizeof(prompt));
        cline_clean_prompt(prompt, cleaned, sizeof(cleaned));

        if (cleaned[0] != '\0') {
            str_truncate_clean(session.summary, cleaned, sizeof(session.summary) - 1);
            session.was_merely_open = false;
        } else {
            snprintf(session.summary, sizeof(session.summary), "Recent Cline session");
            session.was_merely_open = true;
        }
    }

    session.last_active = mtime;
    fs_format_time(mtime, session.last_active_str, sizeof(session.last_active_str));

    config_format_tool_cmd(scan->cfg, "cline", session.id, session.resume_cmd, sizeof(session.resume_cmd));

    git_inspect_workspace(session.workspace, scan->lookback_hours, &session.git);
    session.is_already_running = proc_is_session_running("cline", session.workspace);

    session_list_add(scan->list, &session);

    free(content);
    return 0;
}

static bool cline_is_available(void) {
    char sessions_dir[MAX_PATH_LEN];
    fs_expand_path("~/.cline/data/sessions", sessions_dir, sizeof(sessions_dir));
    return fs_dir_exists(sessions_dir);
}

static int cline_discover(const flint_config_t *cfg, int lookback_hours, session_list_t *list) {
    char sessions_dir[MAX_PATH_LEN];
    fs_expand_path("~/.cline/data/sessions", sessions_dir, sizeof(sessions_dir));
    if (!fs_dir_exists(sessions_dir)) {
        return 0;
    }

    cline_scan_t scan;
    scan.cfg = cfg;
    scan.lookback_hours = lookback_hours;
    scan.cutoff_time = time(NULL) - (lookback_hours * 3600);
    scan.list = list;

    fs_list_dir(sessions_dir, cline_session_dir_cb, &scan);
    return 0;
}

const ai_adapter_t cline_adapter = {
    .name = "Cline",
    .cli_binary = "cline",
    .is_available = cline_is_available,
    .discover_sessions = cline_discover
};
