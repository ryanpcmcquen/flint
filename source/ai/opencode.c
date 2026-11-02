#include "ai_adapter.h"
#include "../git/git.h"
#include "../process/proc.h"
#include "../util/fs.h"
#include "../util/json.h"
#include <stdlib.h>
#include <string.h>

/* OpenCode stores session data in a local SQLite database. The official way to
 * enumerate sessions is `opencode session list --format json`, which queries
 * that database on the background server; we shell out to `sqlite3` to do the
 * same from the C engine. */

static bool opencode_is_available(void) {
    char db[MAX_PATH_LEN];
    fs_expand_path("~/.local/share/opencode/opencode.db", db, sizeof(db));
    return fs_file_exists(db);
}

static int opencode_discover(const flint_config_t *cfg, int lookback_hours,
                             session_list_t *list) {
    char db[MAX_PATH_LEN];
    fs_expand_path("~/.local/share/opencode/opencode.db", db, sizeof(db));

    time_t now = time(NULL);
    long long cutoff = (long long)(now - (lookback_hours * 3600));

    char sql[MAX_CMD_LEN];
    snprintf(sql, sizeof(sql),
             "select json_object('id', id, 'title', title, 'directory', directory, "
             "'time_updated', time_updated, 'model', model, 'agent', agent) "
             "from session_v2 where time_updated >= %lld order by time_updated desc",
             cutoff);

    char cmd[MAX_CMD_LEN * 2];
    snprintf(cmd, sizeof(cmd), "sqlite3 \"%s\" \"%s\"", db, sql);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) return 0;

    char line[MAX_CMD_LEN];
    while (fgets(line, sizeof(line), pipe)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (len == 0) continue;

        session_t session;
        memset(&session, 0, sizeof(session));
        strncpy(session.id, "", sizeof(session.id) - 1);
        session.tool_name[0] = '\0';
        strncpy(session.tool_cmd, "opencode", sizeof(session.tool_cmd) - 1);
        session.origin = SESSION_ORIGIN_USER;

        json_get_string(line, "id", session.id, sizeof(session.id));
        json_get_string(line, "title", session.summary, sizeof(session.summary));
        json_get_string(line, "directory", session.workspace, sizeof(session.workspace));
        json_get_int(line, "time_updated", (long *)&session.last_active);

        if (session.summary[0] == '\0') strncpy(session.summary, "OpenCode session", sizeof(session.summary) - 1);
        if (session.workspace[0] == '\0') strncpy(session.workspace, ".", sizeof(session.workspace) - 1);

        fs_format_time(session.last_active, session.last_active_str, sizeof(session.last_active_str));

        snprintf(session.resume_cmd, sizeof(session.resume_cmd), "opencode --session %s", session.id);

        /* Git context for the workspace. */
        git_inspect_workspace(session.workspace, cfg ? cfg->lookback_hours : 24, &session.git);
        session.is_already_running = proc_is_session_running("opencode", session.workspace);

        session_list_add(list, &session);
    }

    pclose(pipe);
    return 0;
}

const ai_adapter_t opencode_adapter = {
    .name = "OpenCode",
    .cli_binary = "opencode",
    .is_available = opencode_is_available,
    .discover_sessions = opencode_discover
};
