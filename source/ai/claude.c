#include "ai_adapter.h"
#include "../util/fs.h"
#include "../util/json.h"
#include <string.h>

static bool claude_is_available(void) {
    char claude_json[MAX_PATH_LEN];
    char claude_dir[MAX_PATH_LEN];
    fs_expand_path("~/.claude.json", claude_json, sizeof(claude_json));
    fs_expand_path("~/.claude", claude_dir, sizeof(claude_dir));
    return fs_file_exists(claude_json) || fs_dir_exists(claude_dir);
}

typedef struct {
    int lookback_hours;
    time_t cutoff_time;
    session_list_t *list;
} claude_scan_t;

static int claude_project_dir_cb(const char *parent, const char *entry_name, bool is_dir, void *userdata) {
    (void)is_dir;
    claude_scan_t *scan = (claude_scan_t *)userdata;

    char full_path[MAX_PATH_LEN];
    fs_path_join(parent, entry_name, full_path, sizeof(full_path));

    time_t mtime = fs_get_mod_time(full_path);
    if (mtime < scan->cutoff_time) {
        return 0;
    }

    session_t session;
    memset(&session, 0, sizeof(session));
    strncpy(session.id, entry_name, sizeof(session.id) - 1);
    strncpy(session.tool_name, "Claude Code", sizeof(session.tool_name) - 1);
    strncpy(session.tool_cmd, "claude", sizeof(session.tool_cmd) - 1);
    session.last_active = mtime;
    fs_format_time(mtime, session.last_active_str, sizeof(session.last_active_str));

    /* Decode project path if encoded as path-like name or check project metadata */
    if (entry_name[0] == '%') {
        /* Encoded path: replace %2F with / */
        char decoded[MAX_PATH_LEN] = {0};
        const char *s = entry_name;
        char *d = decoded;
        while (*s && (size_t)(d - decoded) < sizeof(decoded) - 1) {
            if (*s == '%' && *(s+1) == '2' && (*(s+2) == 'F' || *(s+2) == 'f')) {
                *d++ = PATH_SEP;
                s += 3;
            } else {
                *d++ = *s++;
            }
        }
        *d = '\0';
        strncpy(session.workspace, decoded, sizeof(session.workspace) - 1);
    } else {
        fs_expand_path("~", session.workspace, sizeof(session.workspace));
    }

    snprintf(session.summary, sizeof(session.summary), "Recent Claude Code activity in %s", entry_name);
    snprintf(session.resume_cmd, sizeof(session.resume_cmd), "claude --resume");

    session_list_add(scan->list, &session);
    return 0;
}

static int claude_discover(int lookback_hours, session_list_t *list) {
    char claude_json[MAX_PATH_LEN];
    fs_expand_path("~/.claude.json", claude_json, sizeof(claude_json));

    time_t now = time(NULL);
    time_t cutoff = now - (lookback_hours * 3600);

    /* Check ~/.claude.json modification or recent projects */
    if (fs_file_exists(claude_json)) {
        time_t mtime = fs_get_mod_time(claude_json);
        if (mtime >= cutoff) {
            size_t size = 0;
            char *content = fs_read_file(claude_json, &size);
            if (content) {
                char projects_block[2048] = {0};
                if (json_get_block(content, "mruProjects", projects_block, sizeof(projects_block)) ||
                    json_get_block(content, "projects", projects_block, sizeof(projects_block))) {
                    /* Extract paths from array */
                    const char *p = projects_block;
                    while ((p = strchr(p, '"')) != NULL) {
                        p++;
                        const char *end = strchr(p, '"');
                        if (!end) break;
                        size_t len = (size_t)(end - p);
                        if (len > 1 && len < MAX_PATH_LEN) {
                            char proj_path[MAX_PATH_LEN];
                            strncpy(proj_path, p, len);
                            proj_path[len] = '\0';

                            if (fs_dir_exists(proj_path)) {
                                session_t session;
                                memset(&session, 0, sizeof(session));
                                strncpy(session.id, "claude-session", sizeof(session.id) - 1);
                                strncpy(session.tool_name, "Claude Code", sizeof(session.tool_name) - 1);
                                strncpy(session.tool_cmd, "claude", sizeof(session.tool_cmd) - 1);
                                strncpy(session.workspace, proj_path, sizeof(session.workspace) - 1);
                                session.last_active = mtime;
                                fs_format_time(mtime, session.last_active_str, sizeof(session.last_active_str));
                                snprintf(session.summary, sizeof(session.summary), "Active Claude Code project");
                                snprintf(session.resume_cmd, sizeof(session.resume_cmd), "claude");

                                session_list_add(list, &session);
                                break; /* Top active MRU project */
                            }
                        }
                        p = end + 1;
                    }
                }
                free(content);
            }
        }
    }

    /* Check ~/.claude/projects/ */
    char projects_dir[MAX_PATH_LEN];
    fs_expand_path("~/.claude/projects", projects_dir, sizeof(projects_dir));
    if (fs_dir_exists(projects_dir)) {
        claude_scan_t scan;
        scan.lookback_hours = lookback_hours;
        scan.cutoff_time = cutoff;
        scan.list = list;
        fs_list_dir(projects_dir, claude_project_dir_cb, &scan);
    }

    return 0;
}

const ai_adapter_t claude_adapter = {
    .name = "Claude Code",
    .cli_binary = "claude",
    .is_available = claude_is_available,
    .discover_sessions = claude_discover
};
