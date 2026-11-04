#include "ai_adapter.h"
#include "../util/fs.h"
#include "../util/json.h"
#include "../config/config.h"
#include "../git/git.h"
#include "../process/proc.h"
#include <string.h>

static bool codex_is_available(void) {
    char codex_dir[MAX_PATH_LEN];
    char openai_dir[MAX_PATH_LEN];
    fs_expand_path("~/.codex", codex_dir, sizeof(codex_dir));
    fs_expand_path("~/.openai", openai_dir, sizeof(openai_dir));
    return fs_dir_exists(codex_dir) || fs_dir_exists(openai_dir);
}

typedef struct {
    const flint_config_t *cfg;
    int lookback_hours;
    time_t cutoff_time;
    session_list_t *list;
} codex_scan_t;

static int codex_dir_cb(const char *parent, const char *entry_name, bool is_dir, void *userdata) {
    (void)is_dir;
    codex_scan_t *scan = (codex_scan_t *)userdata;

    char full_path[MAX_PATH_LEN];
    fs_path_join(parent, entry_name, full_path, sizeof(full_path));

    time_t mtime = fs_get_mod_time(full_path);
    if (mtime < scan->cutoff_time) {
        return 0;
    }

    session_t session;
    memset(&session, 0, sizeof(session));
    strncpy(session.id, entry_name, sizeof(session.id) - 1);
    strncpy(session.tool_name, "ChatGPT Codex", sizeof(session.tool_name) - 1);
    strncpy(session.tool_cmd, "codex", sizeof(session.tool_cmd) - 1);
    session.origin = SESSION_ORIGIN_USER;
    fs_expand_path("~", session.workspace, sizeof(session.workspace));
    session.last_active = mtime;
    fs_format_time(mtime, session.last_active_str, sizeof(session.last_active_str));

    snprintf(session.summary, sizeof(session.summary), "Recent Codex CLI session");
    config_format_tool_cmd(scan->cfg, "codex", session.id, session.resume_cmd, sizeof(session.resume_cmd));

    git_inspect_workspace(session.workspace, scan->lookback_hours, &session.git);
    session.is_already_running = proc_is_session_running("codex", session.workspace);

    session_list_add(scan->list, &session);
    return 0;
}

static int codex_discover(const flint_config_t *cfg, int lookback_hours, session_list_t *list) {
    char codex_dir[MAX_PATH_LEN];
    fs_expand_path("~/.codex", codex_dir, sizeof(codex_dir));

    time_t now = time(NULL);
    time_t cutoff = now - (lookback_hours * 3600);

    if (fs_dir_exists(codex_dir)) {
        codex_scan_t scan;
        scan.cfg = cfg;
        scan.lookback_hours = lookback_hours;
        scan.cutoff_time = cutoff;
        scan.list = list;
        fs_list_dir(codex_dir, codex_dir_cb, &scan);
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
