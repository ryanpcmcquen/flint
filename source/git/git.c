#include "git.h"
#include "../util/fs.h"
#include <string.h>

#ifdef _WIN32
  #define popen _popen
  #define pclose _pclose
#endif

bool git_is_repo(const char *workspace_path) {
    if (!workspace_path) return false;
    char git_dir[MAX_PATH_LEN];
    fs_path_join(workspace_path, ".git", git_dir, sizeof(git_dir));
    return fs_dir_exists(git_dir) || fs_file_exists(git_dir);
}

static int run_git_cmd(const char *workspace, const char *git_args, char *out, size_t out_size) {
    if (!workspace || !git_args || !out || out_size == 0) return -1;
    out[0] = '\0';

    char cmd[MAX_CMD_LEN * 2];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" %s 2>nul", workspace, git_args);
#else
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" %s 2>/dev/null", workspace, git_args);
#endif

    FILE *pipe = popen(cmd, "r");
    if (!pipe) return -1;

    size_t total_read = 0;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) && total_read + 1 < out_size) {
        size_t len = strlen(buffer);
        if (total_read + len >= out_size) {
            len = out_size - total_read - 1;
        }
        memcpy(out + total_read, buffer, len);
        total_read += len;
        out[total_read] = '\0';
    }

    pclose(pipe);
    return 0;
}

int git_inspect_workspace(const char *workspace_path, int lookback_hours, git_info_t *out_info) {
    if (!workspace_path || !out_info) return -1;
    memset(out_info, 0, sizeof(*out_info));

    if (!git_is_repo(workspace_path)) {
        out_info->is_git_repo = false;
        return 0;
    }
    out_info->is_git_repo = true;

    /* 1. Get branch + uncommitted status in a single git call:
          git status --porcelain -b (branch header + change list) */
    char status_out[2048] = {0};
    if (run_git_cmd(workspace_path, "status --porcelain -b", status_out, sizeof(status_out)) == 0) {
        str_trim(status_out);
        int modified = 0;
        int untracked = 0;
        int staged = 0;

        char first_modified_file[MAX_PATH_LEN] = {0};

        const char *p = status_out;
        while (*p) {
            const char *line_start = p;
            while (*p && *p != '\n' && *p != '\r') p++;
            size_t llen = (size_t)(p - line_start);

            if (llen > 0 && line_start[0] == '#') {
                /* Branch header, e.g. "## main...origin/main" or "## No commits yet on main" */
                char header[256];
                if (llen < sizeof(header)) {
                    strncpy(header, line_start, llen);
                    header[llen] = '\0';
                    const char *hb = strstr(header, "## ");
                    if (hb) {
                        const char *b = hb + 3;
                        const char *end = b;
                        while (*end && *end != '\n' && *end != '\r') end++;
                        size_t blen = (size_t)(end - b);
                        if (blen < sizeof(out_info->branch)) {
                            char branch_buf[128];
                            strncpy(branch_buf, b, blen);
                            branch_buf[blen] = '\0';
                            char *dotdot = strstr(branch_buf, "...");
                            if (dotdot) *dotdot = '\0';
                            char *no_br = strstr(branch_buf, " (no branch)");
                            if (no_br) *no_br = '\0';
                            const char *np = "No commits yet on ";
                            size_t nplen = strlen(np);
                            if (strncmp(branch_buf, np, nplen) == 0) {
                                str_trim(branch_buf + nplen);
                            }
                            str_trim(branch_buf);
                            strncpy(out_info->branch, branch_buf, sizeof(out_info->branch) - 1);
                            out_info->branch[sizeof(out_info->branch) - 1] = '\0';
                        }
                    }
                }
            } else if (llen >= 3) {
                /* Uncommitted change line, e.g. " M file", "M  file", "?? file" */
                char x = line_start[0];
                char y = line_start[1];
                if (x == '?' && y == '?') {
                    untracked++;
                } else {
                    if (x != ' ' && x != '?') staged++;
                    if (y != ' ' && y != '?') modified++;
                }

                if (first_modified_file[0] == '\0') {
                    const char *file_start = line_start + 3;
                    size_t flen = (size_t)(p - file_start);
                    if (flen < sizeof(first_modified_file)) {
                        strncpy(first_modified_file, file_start, flen);
                        first_modified_file[flen] = '\0';
                        str_trim(first_modified_file);
                    }
                }
            }

            while (*p == '\r' || *p == '\n') p++;
        }

        out_info->uncommitted_count = staged + modified + untracked;
        if (out_info->uncommitted_count > 0) {
            snprintf(out_info->uncommitted_summary, sizeof(out_info->uncommitted_summary),
                     "%d change(s) [%d modified, %d staged, %d untracked]",
                     out_info->uncommitted_count, modified, staged, untracked);
            if (first_modified_file[0] != '\0') {
                snprintf(out_info->suggested_focus, sizeof(out_info->suggested_focus),
                         "Finish uncommitted changes in %s", first_modified_file);
            }
        } else {
            snprintf(out_info->uncommitted_summary, sizeof(out_info->uncommitted_summary), "Clean working tree");
        }

        if (out_info->branch[0] == '\0') {
            strncpy(out_info->branch, "HEAD", sizeof(out_info->branch) - 1);
            out_info->branch[sizeof(out_info->branch) - 1] = '\0';
        }
    } else {
        strncpy(out_info->branch, "HEAD", sizeof(out_info->branch) - 1);
        out_info->branch[sizeof(out_info->branch) - 1] = '\0';
    }

    /* 2. Get recent commits */
    char log_args[128];
    snprintf(log_args, sizeof(log_args), "log --since=\"%d hours ago\" --oneline -n 5", lookback_hours);

    char log_out[2048] = {0};
    if (run_git_cmd(workspace_path, log_args, log_out, sizeof(log_out)) == 0) {
        str_trim(log_out);
        if (log_out[0] != '\0') {
            strncpy(out_info->recent_commits, log_out, sizeof(out_info->recent_commits) - 1);
            out_info->recent_commits[sizeof(out_info->recent_commits) - 1] = '\0';
            int count = 0;
            const char *lp = log_out;
            while (*lp) {
                if (*lp == '\n') count++;
                lp++;
            }
            out_info->commit_count = count + 1;

            if (out_info->suggested_focus[0] == '\0') {
                /* Pick up from last commit */
                char first_line[256] = {0};
                const char *nl = strchr(log_out, '\n');
                size_t llen = nl ? (size_t)(nl - log_out) : strlen(log_out);
                if (llen >= sizeof(first_line)) llen = sizeof(first_line) - 1;
                strncpy(first_line, log_out, llen);
                first_line[llen] = '\0';
                snprintf(out_info->suggested_focus, sizeof(out_info->suggested_focus),
                         "Continue work from recent commit: %s", first_line);
            }
        }
    }

    if (out_info->suggested_focus[0] == '\0') {
        snprintf(out_info->suggested_focus, sizeof(out_info->suggested_focus),
                 "Review repository status and plan today's tasks on %s", out_info->branch);
    }

    return 0;
}
