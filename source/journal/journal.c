#include "journal.h"
#include "../util/fs.h"
#include "../git/git.h"
#include <string.h>

static void ensure_dir(const char *dir) {
    if (!fs_dir_exists(dir)) {
        if (fs_mkdir_p(dir) != 0) {
            fprintf(stderr, "Error: Failed to create directory: %s\n", dir);
        }
    }
}

/* Push the journal repository to its configured remote (if any). */
static void journal_push_to_remote(const char *repo) {
    if (!repo || repo[0] == '\0') return;

    char check_cmd[MAX_CMD_LEN * 2];
#ifdef _WIN32
    snprintf(check_cmd, sizeof(check_cmd), "git -C \"%s\" remote get-url origin >nul 2>&1", repo);
#else
    snprintf(check_cmd, sizeof(check_cmd), "git -C \"%s\" remote get-url origin >/dev/null 2>&1", repo);
#endif
    if (system(check_cmd) != 0) {
        /* no remote named "origin" is configured */
        return;
    }

    char push_cmd[MAX_CMD_LEN * 2];
#ifdef _WIN32
    snprintf(push_cmd, sizeof(push_cmd), "git -C \"%s\" push origin >nul 2>&1", repo);
#else
    snprintf(push_cmd, sizeof(push_cmd), "git -C \"%s\" push origin >/dev/null 2>&1", repo);
#endif
    system(push_cmd);
}

int journal_record_daily(const flint_config_t *cfg, const session_list_t *list) {
    if (!cfg || !list) return -1;
    if (cfg->journal_repo[0] == '\0') return 0;

    ensure_dir(cfg->journal_repo);

    char daily_dir[MAX_PATH_LEN];
    fs_path_join(cfg->journal_repo, "daily", daily_dir, sizeof(daily_dir));
    ensure_dir(daily_dir);

    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    char date_file[32];
    char date_heading[64];
    if (lt) {
        strftime(date_file, sizeof(date_file), "%Y-%m-%d.md", lt);
        strftime(date_heading, sizeof(date_heading), "%A, %B %d, %Y", lt);
    } else {
        snprintf(date_file, sizeof(date_file), "today.md");
        snprintf(date_heading, sizeof(date_heading), "Today");
    }

    char file_path[MAX_PATH_LEN];
    fs_path_join(daily_dir, date_file, file_path, sizeof(file_path));

    FILE *f = fopen(file_path, "w");
    if (!f) return -1;

    fprintf(f, "# Daily Standup - %s\n\n", date_heading);

    fprintf(f, "## Accomplishments & Work Log\n\n");
    if (list->count == 0) {
        fprintf(f, "_No active AI CLI sessions recorded._\n\n");
    } else {
        for (size_t i = 0; i < list->count; i++) {
            const session_t *s = &list->items[i];
            char ws_tilde[MAX_PATH_LEN];
            char summary_clean[MAX_SUMMARY_LEN];
            fs_tildify_path(s->workspace, ws_tilde, sizeof(ws_tilde));
            fs_redact_home_paths(s->summary, summary_clean, sizeof(summary_clean));

            fprintf(f, "### [%s] `%s`\n", s->tool_name, ws_tilde);
            fprintf(f, "- **Session Origin**: %s\n", (s->origin == SESSION_ORIGIN_USER) ? "User" : "Agent/Subagent");
            fprintf(f, "- **Last Active**: %s\n", s->last_active_str);
            fprintf(f, "- **Accomplishments**: %s\n", summary_clean);

            if (s->git.is_git_repo) {
                fprintf(f, "- **Git Branch**: `%s`\n", s->git.branch);
                if (s->git.commit_count > 0) {
                    fprintf(f, "- **Recent Commits**:\n");
                    const char *p = s->git.recent_commits;
                    while (*p) {
                        const char *lstart = p;
                        while (*p && *p != '\n' && *p != '\r') p++;
                        size_t llen = (size_t)(p - lstart);
                        if (llen > 0) {
                            fprintf(f, "  - `%.*s`\n", (int)llen, lstart);
                        }
                        while (*p == '\r' || *p == '\n') p++;
                    }
                }
                if (s->git.uncommitted_count > 0) {
                    fprintf(f, "- **Uncommitted Changes**: %s\n", s->git.uncommitted_summary);
                }
            }
            fprintf(f, "\n");
        }
    }

    fprintf(f, "## Suggested Focus for Today\n\n");
    bool has_focus = false;
    for (size_t i = 0; i < list->count; i++) {
        const session_t *s = &list->items[i];
        if (s->git.suggested_focus[0] != '\0') {
            char ws_tilde[MAX_PATH_LEN];
            fs_tildify_path(s->workspace, ws_tilde, sizeof(ws_tilde));
            fprintf(f, "- [ ] **%s**: %s\n", ws_tilde, s->git.suggested_focus);
            has_focus = true;
        }
    }
    if (!has_focus) {
        fprintf(f, "- [ ] Plan today's priorities and review active projects\n");
    }
    fprintf(f, "\n");

    fprintf(f, "## Active Sessions to Resume\n\n");
    for (size_t i = 0; i < list->count; i++) {
        const session_t *s = &list->items[i];
        char ws_tilde[MAX_PATH_LEN];
        char cmd_clean[MAX_CMD_LEN];
        fs_tildify_path(s->workspace, ws_tilde, sizeof(ws_tilde));
        fs_redact_home_paths(s->resume_cmd, cmd_clean, sizeof(cmd_clean));
        fprintf(f, "- `%s` in `%s`\n", cmd_clean, ws_tilde);
    }
    fprintf(f, "\n---\n*Logged automatically by [Flint](https://github.com/ryanpcmcquen/flint)*\n");

    fclose(f);
    printf("  " ANSI_GREEN "✓" ANSI_RESET " Daily standup logged to " ANSI_BOLD "%s" ANSI_RESET "\n", file_path);

    /* If the journal is a git repository, commit the log */
    if (git_is_repo(cfg->journal_repo)) {
        char git_cmd[MAX_CMD_LEN * 2];
#ifdef _WIN32
        snprintf(git_cmd, sizeof(git_cmd), "git -C \"%s\" add . && git -C \"%s\" commit -m \"Flint Daily Log: %s\" >nul 2>&1",
                 cfg->journal_repo, cfg->journal_repo, date_file);
#else
        snprintf(git_cmd, sizeof(git_cmd), "git -C \"%s\" add . && git -C \"%s\" commit -m \"Flint Daily Log: %s\" >/dev/null 2>&1",
                 cfg->journal_repo, cfg->journal_repo, date_file);
#endif
        system(git_cmd);
        journal_push_to_remote(cfg->journal_repo);
    }

    return 0;
}

typedef struct {
    int days_limit;
    time_t cutoff_time;
    FILE *out_file;
    int entry_count;
} summary_walker_t;

static int summary_file_cb(const char *parent, const char *entry_name, bool is_dir, void *userdata) {
    if (is_dir) return 0;
    if (!str_has_suffix(entry_name, ".md")) return 0;

    summary_walker_t *w = (summary_walker_t *)userdata;

    char full_path[MAX_PATH_LEN];
    fs_path_join(parent, entry_name, full_path, sizeof(full_path));

    time_t mtime = fs_get_mod_time(full_path);
    if (mtime < w->cutoff_time) return 0;

    size_t size = 0;
    char *content = fs_read_file(full_path, &size);
    if (!content) return 0;

    w->entry_count++;
    fprintf(w->out_file, "\n---\n### Log: %s\n\n", entry_name);
    fputs(content, w->out_file);
    fprintf(w->out_file, "\n");

    /* Also print brief to terminal */
    printf("  " ANSI_CYAN "•" ANSI_RESET " %s (%zu bytes)\n", entry_name, size);

    free(content);
    return 0;
}

int journal_generate_weekly(const flint_config_t *cfg) {
    if (!cfg || cfg->journal_repo[0] == '\0') {
        fprintf(stderr, "No journal_repo configured.\n");
        return -1;
    }

    char daily_dir[MAX_PATH_LEN];
    char weekly_dir[MAX_PATH_LEN];
    fs_path_join(cfg->journal_repo, "daily", daily_dir, sizeof(daily_dir));
    fs_path_join(cfg->journal_repo, "weekly", weekly_dir, sizeof(weekly_dir));
    ensure_dir(weekly_dir);

    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    char out_filename[64];
    if (lt) {
        strftime(out_filename, sizeof(out_filename), "%Y-W%W.md", lt);
    } else {
        snprintf(out_filename, sizeof(out_filename), "weekly_summary.md");
    }

    char out_path[MAX_PATH_LEN];
    fs_path_join(weekly_dir, out_filename, out_path, sizeof(out_path));

    FILE *f = fopen(out_path, "w");
    if (!f) return -1;

    fprintf(f, "# Weekly Standup Summary - %s\n\n", out_filename);

    printf(ANSI_BOLD "\n=== Generating Weekly Standup Summary (%s) ===\n" ANSI_RESET, out_filename);

    summary_walker_t walker;
    walker.days_limit = 7;
    walker.cutoff_time = now - (7 * 24 * 3600);
    walker.out_file = f;
    walker.entry_count = 0;

    fs_list_dir(daily_dir, summary_file_cb, &walker);

    fclose(f);

    printf("\n  " ANSI_BOLD ANSI_GREEN "✓ Compiled %d daily log(s) into " ANSI_CYAN "%s" ANSI_RESET "\n\n",
           walker.entry_count, out_path);

    if (git_is_repo(cfg->journal_repo)) {
        char git_cmd[MAX_CMD_LEN * 2];
        snprintf(git_cmd, sizeof(git_cmd), "git -C \"%s\" add . && git -C \"%s\" commit -m \"Weekly Summary: %s\" >/dev/null 2>&1",
                 cfg->journal_repo, cfg->journal_repo, out_filename);
        system(git_cmd);
        journal_push_to_remote(cfg->journal_repo);
    }

    return 0;
}

int journal_generate_monthly(const flint_config_t *cfg) {
    if (!cfg || cfg->journal_repo[0] == '\0') {
        fprintf(stderr, "No journal_repo configured.\n");
        return -1;
    }

    char daily_dir[MAX_PATH_LEN];
    char monthly_dir[MAX_PATH_LEN];
    fs_path_join(cfg->journal_repo, "daily", daily_dir, sizeof(daily_dir));
    fs_path_join(cfg->journal_repo, "monthly", monthly_dir, sizeof(monthly_dir));
    ensure_dir(monthly_dir);

    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    char out_filename[64];
    if (lt) {
        strftime(out_filename, sizeof(out_filename), "%Y-%m.md", lt);
    } else {
        snprintf(out_filename, sizeof(out_filename), "monthly_summary.md");
    }

    char out_path[MAX_PATH_LEN];
    fs_path_join(monthly_dir, out_filename, out_path, sizeof(out_path));

    FILE *f = fopen(out_path, "w");
    if (!f) return -1;

    fprintf(f, "# Monthly Standup Summary - %s\n\n", out_filename);

    printf(ANSI_BOLD "\n=== Generating Monthly Standup Summary (%s) ===\n" ANSI_RESET, out_filename);

    summary_walker_t walker;
    walker.days_limit = 30;
    walker.cutoff_time = now - (30 * 24 * 3600);
    walker.out_file = f;
    walker.entry_count = 0;

    fs_list_dir(daily_dir, summary_file_cb, &walker);

    fclose(f);

    printf("\n  " ANSI_BOLD ANSI_GREEN "✓ Compiled %d daily log(s) into " ANSI_CYAN "%s" ANSI_RESET "\n\n",
           walker.entry_count, out_path);

    if (git_is_repo(cfg->journal_repo)) {
        char git_cmd[MAX_CMD_LEN * 2];
        snprintf(git_cmd, sizeof(git_cmd), "git -C \"%s\" add . && git -C \"%s\" commit -m \"Monthly Summary: %s\" >/dev/null 2>&1",
                 cfg->journal_repo, cfg->journal_repo, out_filename);
        system(git_cmd);
        journal_push_to_remote(cfg->journal_repo);
    }

    return 0;
}
