#include "ai_adapter.h"
#include "../util/fs.h"
#include "../util/json.h"
#include "../util/exec.h"
#include "../config/config.h"
#include "../git/git.h"
#include "../process/proc.h"
#include <string.h>

#define OPENCLAW_SESSIONS_JSON_MAX 8192

typedef struct {
    const flint_config_t *cfg;
    int lookback_hours;
    time_t cutoff_time;
    session_list_t *list;
} openclaw_scan_t;

static int openclaw_discover(const flint_config_t *cfg, int lookback_hours, session_list_t *list) {
    openclaw_scan_t scan;
    scan.cfg = cfg;
    scan.lookback_hours = lookback_hours;
    scan.cutoff_time = time(NULL) - (lookback_hours * 3600);
    scan.list = list;

    char *json = exec_capture("openclaw sessions --json");
    if (!json) return 0;

    char sessions[OPENCLAW_SESSIONS_JSON_MAX];
    if (json_get_block(json, "sessions", sessions, sizeof(sessions))) {
        const char *obj = sessions;
        const char *end = NULL;
        const char *sess = json_next_object(obj, &end);
        while (sess) {
            char key[MAX_SUMMARY_LEN] = {0};
            char sid[MAX_NAME_LEN] = {0};
            char status[MAX_SUMMARY_LEN] = {0};
            char model[MAX_SUMMARY_LEN] = {0};
            char kind[MAX_SUMMARY_LEN] = {0};
            long long updated = 0;

            json_get_string(sess, "key", key, sizeof(key));
            json_get_string(sess, "sessionId", sid, sizeof(sid));
            json_get_string(sess, "status", status, sizeof(status));
            json_get_string(sess, "model", model, sizeof(model));
            json_get_string(sess, "kind", kind, sizeof(kind));
            json_get_long_long(sess, "updatedAt", &updated);

            if (sid[0] == '\0') {
                strncpy(sid, key, sizeof(sid) - 1);
            }

            time_t ts = (updated > 0) ? ((time_t)updated / 1000) : time(NULL);
            if (ts < scan.cutoff_time) {
                sess = json_next_object(end, &end);
                continue;
            }

            session_t session;
            memset(&session, 0, sizeof(session));
            strncpy(session.id, sid, sizeof(session.id) - 1);
            strncpy(session.tool_name, "OpenClaw", sizeof(session.tool_name) - 1);
            strncpy(session.tool_cmd, "openclaw", sizeof(session.tool_cmd) - 1);

            char workspace[MAX_PATH_LEN] = {0};
            fs_expand_path("~/.openclaw/workspace", workspace, sizeof(workspace));
            if (workspace[0] == '\0') {
                fs_expand_path("~", workspace, sizeof(workspace));
            }
            strncpy(session.workspace, workspace, sizeof(session.workspace) - 1);

            session.origin = (kind[0] != '\0' && strcasecmp(kind, "cron") == 0)
                                 ? SESSION_ORIGIN_AGENT
                                 : SESSION_ORIGIN_USER;

            char summary[MAX_SUMMARY_LEN] = {0};
            if (status[0] != '\0') {
                str_truncate_clean(summary, status, sizeof(summary) - 1);
            } else if (key[0] != '\0') {
                str_truncate_clean(summary, key, sizeof(summary) - 1);
            }
            if (summary[0] == '\0') {
                snprintf(summary, sizeof(summary), "Recent OpenClaw session");
            }
            strncpy(session.summary, summary, sizeof(session.summary) - 1);
            session.was_merely_open = false;

            session.last_active = ts;
            fs_format_time(ts, session.last_active_str, sizeof(session.last_active_str));

            config_format_tool_cmd(cfg, "openclaw", key, session.resume_cmd, sizeof(session.resume_cmd));
            git_inspect_workspace(session.workspace, scan.lookback_hours, &session.git);
            session.is_already_running = proc_is_session_running("openclaw", session.workspace);

            session_list_add(list, &session);

            sess = json_next_object(end, &end);
        }
    }

    free(json);
    return 0;
}

static bool openclaw_is_available(void) {
    char agents_dir[MAX_PATH_LEN];
    fs_expand_path("~/.openclaw/agents", agents_dir, sizeof(agents_dir));
    return fs_dir_exists(agents_dir);
}

const ai_adapter_t openclaw_adapter = {
    .name = "OpenClaw",
    .cli_binary = "openclaw",
    .is_available = openclaw_is_available,
    .discover_sessions = openclaw_discover
};
