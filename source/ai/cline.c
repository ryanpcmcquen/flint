#include "ai_adapter.h"
#include "../git/git.h"
#include "../process/proc.h"
#include "../util/fs.h"
#include "../util/json.h"
#include <stdlib.h>
#include <string.h>

/* Cline CLI (and desktop app) persist agent sessions under
 * ~/.cline/data/sessions/<session-id>/<session-id>.json. Each directory is a
 * session and is identified by its directory name (the session id). */

static bool cline_is_available(void) {
    char path[MAX_PATH_LEN];
    fs_expand_path("~/.cline/data/sessions", path, sizeof(path));
    return fs_dir_exists(path);
}

/* Parse "YYYY-MM-DDTHH:MM:SS[.SSS]Z" into epoch seconds. */
static time_t parse_iso_timestamp(const char *s) {
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    int y, m, d, hh, mm, ss;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &m, &d, &hh, &mm, &ss) == 6) {
        tmv.tm_year = y - 1900;
        tmv.tm_mon = m - 1;
        tmv.tm_mday = d;
        tmv.tm_hour = hh;
        tmv.tm_min = mm;
        tmv.tm_sec = ss;
        tmv.tm_isdst = -1;
        return mktime(&tmv);
    }
    return 0;
}

/* Extract prompt text from a Cline session file and build a session entry. */
static void add_cline_session(const char *session_file, const char *session_id,
                               const flint_config_t *cfg, int lookback_hours,
                               session_list_t *list) {
    (void)cfg;  /* cfg reserved for future tool-specific settings */
    session_t session;
    memset(&session, 0, sizeof(session));
    strncpy(session.id, session_id, sizeof(session.id) - 1);
    strncpy(session.tool_name, "Cline", sizeof(session.tool_name) - 1);
    strncpy(session.tool_cmd, "cline", sizeof(session.tool_cmd) - 1);
    session.origin = SESSION_ORIGIN_USER;

    size_t size = 0;
    char *content = fs_read_file(session_file, &size);
    if (!content) return;

    char tmp[1024];
    if (json_get_string(content, "started_at", tmp, sizeof(tmp))) {
        session.last_active = parse_iso_timestamp(tmp);
        if (session.last_active == 0) session.last_active = fs_get_mod_time(session_file);
    } else {
        session.last_active = fs_get_mod_time(session_file);
    }
    fs_format_time(session.last_active, session.last_active_str, sizeof(session.last_active_str));

    /* Workspace: prefer workspace_root, then cwd, else home. */
    if (json_get_string(content, "workspace_root", tmp, sizeof(tmp)) && tmp[0] != '\0') {
        strncpy(session.workspace, tmp, sizeof(session.workspace) - 1);
    } else if (json_get_string(content, "cwd", tmp, sizeof(tmp)) && tmp[0] != '\0') {
        strncpy(session.workspace, tmp, sizeof(session.workspace) - 1);
    } else {
        fs_expand_path("~", session.workspace, sizeof(session.workspace));
    }

    /* Prompt is wrapped in <user_input ...>...</user_input>; unwrap it. */
    if (json_get_string(content, "prompt", tmp, sizeof(tmp))) {
        const char *body = tmp;
        const char *tag = "<user_input";
        const char *b = strstr(tmp, tag);
        if (b) {
            b = strchr(b, '>');
            if (b) body = b + 1;
            const char *close = strstr(body, "</user_input>");
            if (close) body = close;
        }
        str_truncate_clean(session.summary, body, sizeof(session.summary) - 1);
        if (session.summary[0] == '\0') strncpy(session.summary, "Cline session", sizeof(session.summary) - 1);
    } else {
        strncpy(session.summary, "Cline session", sizeof(session.summary) - 1);
    }

    snprintf(session.resume_cmd, sizeof(session.resume_cmd), "cline --id %s", session_id);

    git_inspect_workspace(session.workspace, lookback_hours, &session.git);
    session.is_already_running = proc_is_session_running("cline", session.workspace);
    session_list_add(list, &session);
    free(content);
}

typedef struct {
    const flint_config_t *cfg;
    int lookback_hours;
    time_t cutoff_time;
    session_list_t *list;
} cline_scan_t;

static int cline_session_cb(const char *parent, const char *entry_name, bool is_dir, void *userdata) {
    (void)parent;
    if (!is_dir) return 0;

    cline_scan_t *scan = (cline_scan_t *)userdata;
    char session_file[MAX_PATH_LEN];
    fs_path_join(parent, entry_name, session_file, sizeof(session_file));
    fs_path_join(session_file, ".json", session_file, sizeof(session_file));

    add_cline_session(session_file, entry_name, scan->cfg, scan->lookback_hours, scan->list);
    return 0;
}

static int cline_discover(const flint_config_t *cfg, int lookback_hours, session_list_t *list) {
    cline_scan_t scan;
    scan.cfg = cfg;
    scan.lookback_hours = lookback_hours;
    scan.list = list;
    time_t now = time(NULL);
    scan.cutoff_time = now - (lookback_hours * 3600);

    char sessions_dir[MAX_PATH_LEN];
    fs_expand_path("~/.cline/data/sessions", sessions_dir, sizeof(sessions_dir));
    fs_list_dir(sessions_dir, cline_session_cb, &scan);
    return 0;
}

const ai_adapter_t cline_adapter = {
    .name = "Cline",
    .cli_binary = "cline",
    .is_available = cline_is_available,
    .discover_sessions = cline_discover
};
