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
    snprintf(push_cmd, sizeof(push_cmd), "git -C \"%s\" push --quiet -u origin HEAD", repo);
    int rc = system(push_cmd);
    if (rc != 0) {
        fprintf(stderr,
                "  " ANSI_YELLOW "Journal push failed. Run: git -C \"%s\" push -u origin HEAD" ANSI_RESET "\n",
                repo);
    }
}

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} journal_index_t;

static int journal_index_cb(const char *parent, const char *entry_name,
                            bool is_dir, void *userdata) {
    (void)parent;
    if (is_dir || !str_has_suffix(entry_name, ".md")) return 0;

    journal_index_t *index = (journal_index_t *)userdata;
    if (index->count >= index->capacity) {
        size_t new_capacity = index->capacity ? index->capacity * 2 : 16;
        char **grown = (char **)realloc(index->items, new_capacity * sizeof(char *));
        if (!grown) return 0;
        index->items = grown;
        index->capacity = new_capacity;
    }

    size_t len = strlen(entry_name);
    char *copy = (char *)malloc(len + 1);
    if (!copy) return 0;
    memcpy(copy, entry_name, len + 1);
    index->items[index->count++] = copy;
    return 0;
}

static int journal_index_cmp_desc(const void *a, const void *b) {
    const char *name_a = *(const char *const *)a;
    const char *name_b = *(const char *const *)b;
    return strcmp(name_b, name_a);
}

static void journal_index_free(journal_index_t *index) {
    for (size_t i = 0; i < index->count; i++) {
        free(index->items[i]);
    }
    free(index->items);
    memset(index, 0, sizeof(*index));
}

static void journal_write_index_section(FILE *f, const char *repo,
                                        const char *dir_name, const char *heading) {
    journal_index_t index;
    memset(&index, 0, sizeof(index));

    char dir_path[MAX_PATH_LEN];
    fs_path_join(repo, dir_name, dir_path, sizeof(dir_path));
    fs_list_dir(dir_path, journal_index_cb, &index);
    qsort(index.items, index.count, sizeof(char *), journal_index_cmp_desc);

    fprintf(f, "## %s\n\n", heading);
    if (index.count == 0) {
        fprintf(f, "_No entries yet._\n\n");
    } else {
        for (size_t i = 0; i < index.count; i++) {
            const char *name = index.items[i];
            size_t len = strlen(name);
            if (len > 3 && strcmp(name + len - 3, ".md") == 0) len -= 3;
            fprintf(f, "- [%.*s](%s/%s)\n", (int)len, name, dir_name, name);
        }
        fprintf(f, "\n");
    }

    journal_index_free(&index);
}

static void journal_write_readme(const char *repo) {
    if (!repo || repo[0] == '\0') return;

    char readme_path[MAX_PATH_LEN];
    fs_path_join(repo, "README.md", readme_path, sizeof(readme_path));
    FILE *f = fopen(readme_path, "w");
    if (!f) return;

    fprintf(f, "# Flint Journal\n\n");
    fprintf(f, "Daily, weekly, and monthly work logs recorded by ");
    fprintf(f, "[Flint](https://github.com/ryanpcmcquen/flint).\n\n");
    fprintf(f, "## Index\n\n");
    fprintf(f, "- [Daily logs](#daily-logs)\n");
    fprintf(f, "- [Weekly summaries](#weekly-summaries)\n");
    fprintf(f, "- [Monthly summaries](#monthly-summaries)\n\n");

    journal_write_index_section(f, repo, "daily", "Daily logs");
    journal_write_index_section(f, repo, "weekly", "Weekly summaries");
    journal_write_index_section(f, repo, "monthly", "Monthly summaries");

    fclose(f);
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
    journal_write_readme(cfg->journal_repo);
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
    journal_write_readme(cfg->journal_repo);

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
    journal_write_readme(cfg->journal_repo);

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
