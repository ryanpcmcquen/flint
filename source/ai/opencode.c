#include "ai_adapter.h"
#include "../util/fs.h"
#include "../util/json.h"
#include "../util/exec.h"
#include "../config/config.h"
#include "../git/git.h"
#include "../process/proc.h"
#include <string.h>

static void opencode_parse_sessions(const char *json, const flint_config_t *cfg,
                                    int lookback_hours, session_list_t *list) {
    if (!json || !cfg || !list) return;

    time_t cutoff = time(NULL) - (lookback_hours * 3600);

    char *data = (char *)malloc(strlen(json) + 1);
    const char *scan = json;
    if (data) {
        data[0] = '\0';
        if (json_get_block(json, "data", data, strlen(json) + 1)) {
            scan = data;
        }
    }

    const char *p = scan;
    while (true) {
        const char *end = NULL;
        const char *object = json_next_object(p, &end);
        if (!object) break;
        p = end;

        size_t len = (size_t)(end - object);
        char *session_json = (char *)malloc(len + 1);
        if (!session_json) break;

        memcpy(session_json, object, len);
        session_json[len] = '\0';

        char id[MAX_NAME_LEN] = {0};
        if (json_get_string(session_json, "id", id, sizeof(id))) {
            char title[MAX_SUMMARY_LEN] = {0};
            char directory[MAX_PATH_LEN] = {0};
            char parent_id[MAX_NAME_LEN] = {0};
            long long updated_ms = 0;

            json_get_string(session_json, "title", title, sizeof(title));
            json_get_string(session_json, "directory", directory, sizeof(directory));
            json_get_string(session_json, "parentID", parent_id, sizeof(parent_id));

            if (json_get_long_long(session_json, "updated", &updated_ms)) {
                time_t updated = (time_t)(updated_ms / 1000);
                if (updated >= cutoff) {
                    session_t session;
                    memset(&session, 0, sizeof(session));

                    strncpy(session.id, id, sizeof(session.id) - 1);
                    strncpy(session.tool_name, "OpenCode", sizeof(session.tool_name) - 1);
                    strncpy(session.tool_cmd, "opencode", sizeof(session.tool_cmd) - 1);
                    session.origin = parent_id[0] != '\0' ?
                                     SESSION_ORIGIN_AGENT : SESSION_ORIGIN_USER;

                    if (directory[0] != '\0') {
                        strncpy(session.workspace, directory, sizeof(session.workspace) - 1);
                    } else {
                        fs_expand_path("~", session.workspace, sizeof(session.workspace));
                    }

                    if (title[0] != '\0') {
                        str_truncate_clean(session.summary, title, sizeof(session.summary) - 1);
                    } else {
                        snprintf(session.summary, sizeof(session.summary), "Recent OpenCode session");
                    }

                    session.last_active = updated;
                    fs_format_time(updated, session.last_active_str, sizeof(session.last_active_str));
                    session.was_merely_open = false;

                    config_format_tool_cmd(cfg, "opencode", session.id,
                                           session.resume_cmd, sizeof(session.resume_cmd));

                    git_inspect_workspace(session.workspace, lookback_hours, &session.git);
                    session.is_already_running = proc_is_session_running("opencode", session.workspace);

                    session_list_add(list, &session);
                }
            }
        }

        free(session_json);
    }

    free(data);
}

static int opencode_discover(const flint_config_t *cfg, int lookback_hours, session_list_t *list) {
    char *output = exec_capture("opencode api session.list --param limit=100");
    if (!output) return 0;

    opencode_parse_sessions(output, cfg, lookback_hours, list);
    free(output);
    return 0;
}

#ifdef TEST_MODE
int flint_opencode_parse_json(const char *json, const flint_config_t *cfg,
                              int lookback_hours, session_list_t *list) {
    if (!json || !cfg || !list) return -1;
    opencode_parse_sessions(json, cfg, lookback_hours, list);
    return 0;
}
#endif

static bool opencode_is_available(void) {
    char db_path[MAX_PATH_LEN];
    fs_expand_path("~/.local/share/opencode/opencode.db", db_path, sizeof(db_path));
    if (fs_file_exists(db_path)) {
        return true;
    }

#ifdef _WIN32
    return system("where opencode >nul 2>&1") == 0;
#else
    return system("command -v opencode >/dev/null 2>&1") == 0;
#endif
}

const ai_adapter_t opencode_adapter = {
    .name = "OpenCode",
    .cli_binary = "opencode",
    .is_available = opencode_is_available,
    .discover_sessions = opencode_discover
};
