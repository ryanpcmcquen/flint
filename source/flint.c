#include "flint.h"
#include "ai/ai_adapter.h"
#include "ai/mcp.h"
#include "brief/brief.h"
#include "terminal/terminal.h"
#include "config/config.h"
#include "git/git.h"
#include "journal/journal.h"
#include "process/proc.h"
#include "util/exec.h"
#include "util/fs.h"
#include "util/json.h"

#include <stdlib.h>

#ifdef _WIN32
  #include <io.h>
  #define isatty _isatty
  #define fileno _fileno
#else
  #include <unistd.h>
#endif

/* Strict positive-integer parse. Returns false on empty/invalid input. */
#ifndef TEST_MODE
static bool parse_positive_int(const char *s, int *out) {
    if (!s || !s[0]) return false;
    char *end = NULL;
    long val = strtol(s, &end, 10);
    if (end == s || (end && *end != '\0')) return false;
    if (val <= 0 || val > 87600) return false;
    *out = (int)val;
    return true;
}
#endif

void session_list_init(session_list_t *list) {
    if (!list) return;
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void session_list_add(session_list_t *list, const session_t *session) {
    if (!list || !session) return;

    /* Deduplicate by tool and session id. Different sessions in the same
     * workspace are distinct and should all remain resumable. If an adapter
     * has no id, fall back to workspace+tool deduplication. */
    for (size_t i = 0; i < list->count; i++) {
        bool same_tool = strcmp(list->items[i].tool_cmd, session->tool_cmd) == 0;
        bool same_session = false;

        if (same_tool) {
            if (session->id[0] != '\0' && list->items[i].id[0] != '\0') {
                same_session = strcmp(list->items[i].id, session->id) == 0;
            } else {
                same_session = strcmp(list->items[i].workspace, session->workspace) == 0;
            }
        }

        if (same_session) {
            if (session->last_active > list->items[i].last_active) {
                list->items[i] = *session;
            }
            return;
        }
    }

    if (list->count >= list->capacity) {
        size_t new_cap = (list->capacity == 0) ? 8 : list->capacity * 2;
        session_t *new_items = (session_t *)realloc(list->items, new_cap * sizeof(session_t));
        if (!new_items) return;
        list->items = new_items;
        list->capacity = new_cap;
    }

    list->items[list->count++] = *session;
}

static int session_recency_cmp(const void *a, const void *b) {
    const session_t *sa = (const session_t *)a;
    const session_t *sb = (const session_t *)b;
    if (sa->last_active < sb->last_active) return 1;
    if (sa->last_active > sb->last_active) return -1;
    return 0;
}

void session_list_sort_by_recency(session_list_t *list) {
    if (!list || list->count < 2) return;
    qsort(list->items, list->count, sizeof(session_t), session_recency_cmp);
}

void session_list_free(session_list_t *list) {
    if (!list) return;
    if (list->items) {
        free(list->items);
        list->items = NULL;
    }
    list->count = 0;
    list->capacity = 0;
}

bool resume_session_should_include(const session_t *s, resume_mode_t mode) {
    if (!s) return false;
    if (mode == RESUME_MODE_USER) return s->origin == SESSION_ORIGIN_USER;
    if (mode == RESUME_MODE_AGENT) return s->origin == SESSION_ORIGIN_AGENT;
    /* RESUME_MODE_ALL */
    return true;
}

bool parse_session_selection(const char *input, size_t count, bool *selected) {
    if (!selected) return false;
    for (size_t i = 0; i < count; i++) selected[i] = false;

    char buf[256];
    if (!input) buf[0] = '\0';
    else {
        strncpy(buf, input, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        str_trim(buf);
    }

    if (buf[0] == '\0' || strcmp(buf, "a") == 0 || strcmp(buf, "all") == 0 ||
        strcmp(buf, "*") == 0) {
        for (size_t i = 0; i < count; i++) selected[i] = true;
        return true;
    }

    if (strcmp(buf, "n") == 0 || strcmp(buf, "none") == 0) {
        return true;
    }

    const char *p = buf;
    bool any = false;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (*p == '\0') break;
        if (*p < '0' || *p > '9') return false;

        char *end = NULL;
        unsigned long start = strtoul(p, &end, 10);
        if (end == p) return false;
        p = end;

        unsigned long finish = start;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '-') {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            if (*p < '0' || *p > '9') return false;
            finish = strtoul(p, &end, 10);
            if (end == p) return false;
            p = end;
        }

        if (start == 0 || finish == 0 || start > finish) return false;
        if (start > count || finish > count) return false;
        for (unsigned long i = start; i <= finish; i++) {
            selected[i - 1] = true;
        }
        any = true;

        while (*p == ' ' || *p == '\t') p++;
        if (*p != '\0' && *p != ',') return false;
    }

    return any;
}

#ifndef TEST_MODE

#define BANNER_INNER_WIDTH 62

static void print_banner_line(const char *text, const char *style) {
    size_t len = strlen(text);
    size_t left = 0;
    size_t right = 0;

    if (len < BANNER_INNER_WIDTH) {
        left = (BANNER_INNER_WIDTH - len) / 2;
        right = BANNER_INNER_WIDTH - len - left;
    }

    printf(ANSI_BOLD ANSI_CYAN "  ║%*s", (int)left, "");
    printf("%s%s", style, text);
    printf(ANSI_RESET ANSI_BOLD ANSI_CYAN "%*s║\n" ANSI_RESET, (int)right, "");
}

static void print_banner(void) {
    time_t now = time(NULL);
    char date_buf[64];
    struct tm *lt = localtime(&now);
    if (lt) {
        strftime(date_buf, sizeof(date_buf), "%A, %B %d, %Y", lt);
    } else {
        snprintf(date_buf, sizeof(date_buf), "Today");
    }

    printf("\n");
    printf(ANSI_BOLD ANSI_CYAN "  ╔══════════════════════════════════════════════════════════════╗\n" ANSI_RESET);
    print_banner_line("F L I N T", ANSI_YELLOW);
    print_banner_line("Morning Standup Brief & AI Session Manager", "");
    print_banner_line(date_buf, ANSI_DIM);
    printf(ANSI_BOLD ANSI_CYAN "  ╚══════════════════════════════════════════════════════════════╝\n\n" ANSI_RESET);
}

static void print_usage(const char *prog_name) {
    printf(ANSI_BOLD "Usage:" ANSI_RESET " %s [options]\n\n", prog_name);
    printf(ANSI_BOLD "Options:\n" ANSI_RESET);
    printf("  -s, --summary           Display daily summary only (do not launch sessions)\n");
    printf("      --json              Output machine-readable JSON (no launch or journal)\n");
    printf("  -r, --resume            Automatically resume all active sessions without prompting\n");
    printf("      --limit <N>         Resume only the N most recent sessions\n");
    printf("  -t, --terminal <name>   Target terminal client (iterm2, ghostty, kitty, apple-terminal,\n");
    printf("                          gnome-terminal, alacritty, konsole, windows-terminal, powershell)\n");
    printf("      --wrapper <name>    Prepend this launcher to every resume command\n");
    printf("  -d, --days <N>          Look back N days (default: 1 day / 24 hours)\n");
    printf("      --hours <H>         Look back H hours\n");
    printf("  -w, --workspace <path>  Filter sessions to specific workspace directory\n");
    printf("  -j, --journal           Record daily standup log to markdown Git repository\n");
    printf("      --weekly            Generate and display weekly standup summary\n");
    printf("      --monthly           Generate and display monthly standup summary\n");
    printf("      --mcp               Query connected MCP tools (e.g. Slack) for today's focus\n");
    printf("      --no-mcp            Skip MCP queries for this run\n");
    printf("  -c, --config <path>     Path to custom flint configuration file\n");
    printf("      --init-config       Generate default ~/.config/flint/config template\n");
    printf("  -n, --dry-run           Show commands that would be executed without launching\n");
    printf("  -l, --list-terminals    List all supported and detected terminal clients\n");
    printf("  -v, --version           Print Flint version\n");
    printf("  -h, --help              Print this help menu\n\n");
}

static int parse_args(int argc, char **argv, flint_options_t *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->mcp_focus_override = -1;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            printf("Flint version %s (C99)\n", FLINT_VERSION);
            exit(0);
        } else if (strcmp(arg, "-s") == 0 || strcmp(arg, "--summary") == 0) {
            opts->summary_only = true;
        } else if (strcmp(arg, "--json") == 0) {
            opts->json_output = true;
            opts->summary_only = true;
        } else if (strcmp(arg, "-r") == 0 || strcmp(arg, "--resume") == 0) {
            opts->resume_now = true;
        } else if (strcmp(arg, "-n") == 0 || strcmp(arg, "--dry-run") == 0) {
            opts->dry_run = true;
        } else if (strcmp(arg, "-j") == 0 || strcmp(arg, "--journal") == 0) {
            opts->write_journal = true;
        } else if (strcmp(arg, "--weekly") == 0) {
            opts->weekly_summary = true;
        } else if (strcmp(arg, "--monthly") == 0) {
            opts->monthly_summary = true;
        } else if (strcmp(arg, "--mcp") == 0) {
            opts->mcp_focus_override = 1;
        } else if (strcmp(arg, "--no-mcp") == 0) {
            opts->mcp_focus_override = 0;
        } else if (strcmp(arg, "-l") == 0 || strcmp(arg, "--list-terminals") == 0) {
            opts->list_terminals = true;
        } else if (strcmp(arg, "--init-config") == 0) {
            char cfg_path[MAX_PATH_LEN];
            fs_expand_path("~/.config/flint/config", cfg_path, sizeof(cfg_path));
            char cfg_dir[MAX_PATH_LEN];
            fs_expand_path("~/.config/flint", cfg_dir, sizeof(cfg_dir));
            if (fs_mkdir_p(cfg_dir) != 0) {
                fprintf(stderr, ANSI_RED "Error: Failed to create config directory: %s\n" ANSI_RESET, cfg_dir);
                exit(1);
            }

            if (config_create_default_file(cfg_path)) {
                printf(ANSI_GREEN "Created template configuration at %s\n" ANSI_RESET, cfg_path);
            } else {
                fprintf(stderr, ANSI_RED "Failed to create configuration file at %s\n" ANSI_RESET, cfg_path);
            }
            exit(0);
        } else if ((strcmp(arg, "-c") == 0 || strcmp(arg, "--config") == 0) && i + 1 < argc) {
            strncpy(opts->config_path, argv[++i], sizeof(opts->config_path) - 1);
        } else if ((strcmp(arg, "-t") == 0 || strcmp(arg, "--terminal") == 0) && i + 1 < argc) {
            strncpy(opts->target_terminal, argv[++i], sizeof(opts->target_terminal) - 1);
        } else if (strcmp(arg, "--wrapper") == 0 && i + 1 < argc) {
            strncpy(opts->wrapper_override, argv[++i], sizeof(opts->wrapper_override) - 1);
        } else if ((strcmp(arg, "-d") == 0 || strcmp(arg, "--days") == 0) && i + 1 < argc) {
            int days = 0;
            if (parse_positive_int(argv[++i], &days)) opts->lookback_hours = days * 24;
        } else if (strcmp(arg, "--hours") == 0 && i + 1 < argc) {
            int hours = 0;
            if (parse_positive_int(argv[++i], &hours)) opts->lookback_hours = hours;
        } else if (strcmp(arg, "--limit") == 0 && i + 1 < argc) {
            int limit = 0;
            if (parse_positive_int(argv[++i], &limit)) opts->resume_limit = limit;
        } else if ((strcmp(arg, "-w") == 0 || strcmp(arg, "--workspace") == 0) && i + 1 < argc) {
            fs_expand_path(argv[++i], opts->workspace_filter, sizeof(opts->workspace_filter));
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg);
            print_usage(argv[0]);
            return -1;
        }
    }
    return 0;
}

static void print_summary(const session_list_t *list) {
    if (list->count == 0) {
        printf(ANSI_YELLOW "  No active AI CLI sessions found in the lookback window.\n" ANSI_RESET);
        printf(ANSI_DIM "  (Run Flint with --days 2 or --days 7 to search further back)\n\n" ANSI_RESET);
        return;
    }

    printf(ANSI_BOLD "  Session details\n" ANSI_RESET);
    printf("  " ANSI_DIM "────────────────────────────────────────────────────────────" ANSI_RESET "\n");

    for (size_t i = 0; i < list->count; i++) {
        const session_t *s = &list->items[i];
        const char *origin_str = (s->origin == SESSION_ORIGIN_USER) ? ANSI_BLUE "User" ANSI_RESET : ANSI_MAGENTA "Subagent" ANSI_RESET;
        printf("\n  " ANSI_BOLD ANSI_CYAN "[%s]" ANSI_RESET " %s  (%s)\n", s->tool_name, s->workspace, origin_str);
        printf("    " ANSI_DIM "Last active:" ANSI_RESET " %s\n", s->last_active_str);

        if (s->was_merely_open) {
            printf("    " ANSI_YELLOW "⚡ " ANSI_RESET "%s\n", s->summary);
        } else {
            printf("    " ANSI_BOLD ANSI_GREEN "✓ " ANSI_RESET "%s\n", s->summary);
        }

        /* Git details */
        if (s->git.is_git_repo) {
            printf("    " ANSI_DIM "Git branch:" ANSI_RESET " %s" ANSI_RESET "  " ANSI_DIM "(%s)" ANSI_RESET "\n",
                   s->git.branch, s->git.uncommitted_summary);
            if (s->git.commit_count > 0) {
                printf("    " ANSI_DIM "Recent commits:" ANSI_RESET "\n");
                const char *p = s->git.recent_commits;
                while (*p) {
                    const char *lstart = p;
                    while (*p && *p != '\n' && *p != '\r') p++;
                    size_t llen = (size_t)(p - lstart);
                    if (llen > 0) {
                        printf("      " ANSI_DIM "•" ANSI_RESET " %.*s\n", (int)llen, lstart);
                    }
                    while (*p == '\r' || *p == '\n') p++;
                }
            }
        }

        if (s->is_already_running) {
            printf("    " ANSI_CYAN "ℹ Process is currently running" ANSI_RESET "\n");
        } else {
            printf("    " ANSI_DIM "Resume command:" ANSI_RESET " cd \"%s\" && %s\n", s->workspace, s->resume_cmd);
        }
    }

    printf("\n  " ANSI_DIM "────────────────────────────────────────────────────────────" ANSI_RESET "\n\n");
}

static void json_print_string(const char *text) {
    const char *value = text ? text : "";
    size_t len = strlen(value);
    size_t size = len * 6 + 1;
    char *escaped = (char *)malloc(size);
    if (!escaped) {
        printf("\"\"");
        return;
    }
    json_escape_string(value, escaped, size);
    printf("\"%s\"", escaped);
    free(escaped);
}

static void json_workspace_label(const char *path, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (!path || path[0] == '\0') {
        snprintf(out, out_size, "unknown workspace");
        return;
    }

    const char *last = strrchr(path, '/');
#ifdef _WIN32
    const char *back = strrchr(path, '\\');
    if (back && (!last || back > last)) last = back;
#endif
    const char *base = last ? last + 1 : path;
    if (base[0] == '\0') base = path;
    snprintf(out, out_size, "%s", base);
}

static void json_print_git(const git_info_t *git) {
    printf("{\"is_repo\":%s,\"branch\":", git->is_git_repo ? "true" : "false");
    json_print_string(git->branch);
    printf(",\"uncommitted_count\":%d,\"commit_count\":%d,\"suggested_focus\":",
           git->uncommitted_count, git->commit_count);
    json_print_string(git->suggested_focus);
    printf("}");
}

static void json_print_session(const session_t *s) {
    printf("{\"id\":");
    json_print_string(s->id);
    printf(",\"tool\":");
    json_print_string(s->tool_name);
    printf(",\"tool_cmd\":");
    json_print_string(s->tool_cmd);
    printf(",\"workspace\":");
    json_print_string(s->workspace);
    printf(",\"origin\":\"%s\"", s->origin == SESSION_ORIGIN_AGENT ? "agent" : "user");
    printf(",\"last_active\":");
    json_print_string(s->last_active_str);
    printf(",\"last_active_epoch\":%lld", (long long)s->last_active);
    printf(",\"summary\":");
    json_print_string(s->summary);
    printf(",\"resume_cmd\":");
    json_print_string(s->resume_cmd);
    printf(",\"is_running\":%s", s->is_already_running ? "true" : "false");
    printf(",\"git\":");
    json_print_git(&s->git);
    printf("}");
}

static void print_json_output(const session_list_t *list, const brief_t *brief,
                              const flint_config_t *cfg, int lookback_hours) {
    size_t session_count = list ? list->count : 0;
    bool *done = session_count ? (bool *)calloc(session_count, sizeof(bool)) : NULL;

    size_t workspace_count = 0;
    for (size_t i = 0; i < session_count; i++) {
        bool already_seen = false;
        for (size_t j = 0; j < i; j++) {
            if (strcmp(list->items[i].workspace, list->items[j].workspace) == 0) {
                already_seen = true;
                break;
            }
        }
        if (!already_seen) workspace_count++;
    }

    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    char generated_at[64];
    if (lt) {
        strftime(generated_at, sizeof(generated_at), "%Y-%m-%dT%H:%M:%S%z", lt);
    } else {
        snprintf(generated_at, sizeof(generated_at), "%lld", (long long)now);
    }

    const char *mode = (cfg->resume_mode == RESUME_MODE_ALL) ? "all" :
                       (cfg->resume_mode == RESUME_MODE_AGENT) ? "agent" : "user";

    printf("{\"version\":");
    json_print_string(FLINT_VERSION);
    printf(",\"generated_at\":");
    json_print_string(generated_at);
    printf(",\"lookback_hours\":%d", lookback_hours);
    printf(",\"resume_mode\":\"%s\"", mode);
    printf(",\"counts\":{\"workspaces\":%zu,\"sessions\":%zu,\"focus\":%zu}",
           workspace_count, session_count, brief ? brief->focus_count : 0);

    printf(",\"workspaces\":[");
    bool first_workspace = true;
    for (size_t i = 0; i < session_count; i++) {
        if (done && done[i]) continue;
        const session_t *workspace_session = &list->items[i];
        if (!first_workspace) printf(",");
        first_workspace = false;

        char label[MAX_PATH_LEN];
        json_workspace_label(workspace_session->workspace, label, sizeof(label));

        printf("{\"path\":");
        json_print_string(workspace_session->workspace);
        printf(",\"label\":");
        json_print_string(label);
        printf(",\"git\":");
        json_print_git(&workspace_session->git);
        printf(",\"sessions\":[");

        bool first_session = true;
        for (size_t j = i; j < session_count; j++) {
            if (strcmp(list->items[j].workspace, workspace_session->workspace) != 0) continue;
            if (done) done[j] = true;
            if (!first_session) printf(",");
            first_session = false;
            json_print_session(&list->items[j]);
        }
        printf("]}");
    }
    printf("],\"focus\":[");

    if (brief) {
        for (size_t i = 0; i < brief->focus_count; i++) {
            const focus_item_t *item = &brief->focus[i];
            char label[MAX_PATH_LEN];
            json_workspace_label(item->workspace, label, sizeof(label));
            if (i > 0) printf(",");
            printf("{\"rank\":%zu,\"workspace\":", i + 1);
            json_print_string(item->workspace);
            printf(",\"workspace_label\":");
            json_print_string(label);
            printf(",\"text\":");
            json_print_string(item->text);
            printf("}");
        }
    }

    printf("]}\n");

    free(done);
}

static bool tool_is_logged_out(const char *tool_cmd) {
    if (!tool_cmd || !exec_command_exists(tool_cmd)) return false;

    if (strcmp(tool_cmd, "claude") == 0) {
        if (getenv("ANTHROPIC_API_KEY") || getenv("CLAUDE_CODE_OAUTH_TOKEN")) {
            return false;
        }
        char *output = exec_capture("claude auth status");
        if (!output) return false;
        bool logged_in = false;
        bool parsed = json_get_bool(output, "loggedIn", &logged_in);
        free(output);
        return parsed && !logged_in;
    }

    if (strcmp(tool_cmd, "codex") == 0) {
        if (getenv("OPENAI_API_KEY") || getenv("CODEX_API_KEY")) {
            return false;
        }
#ifdef _WIN32
        return system("codex login status >nul 2>&1") != 0;
#else
        return system("codex login status >/dev/null 2>&1") != 0;
#endif
    }

    return false;
}

static bool prompt_resume_selection(const session_list_t *list, session_list_t *chosen) {
    if (!list || list->count == 0 || !chosen) return false;

    printf("\n  " ANSI_BOLD "Select sessions to resume:" ANSI_RESET "\n\n");
    for (size_t i = 0; i < list->count; i++) {
        const session_t *s = &list->items[i];
        char workspace[MAX_PATH_LEN];
        char summary[96];
        fs_tildify_path(s->workspace, workspace, sizeof(workspace));
        str_truncate_clean(summary, s->summary, sizeof(summary));
        if (summary[0] == '\0') {
            snprintf(summary, sizeof(summary), "Recent session");
        }
        printf("   %2zu. " ANSI_CYAN "[%s]" ANSI_RESET " %s — %s\n",
               i + 1, s->tool_cmd, workspace, summary);
    }
    printf("\n  Enter numbers/ranges (e.g. 1,3-4), 'a' for all, 'n' for none [a]: ");
    fflush(stdout);

    char input[256] = {0};
    if (!fgets(input, sizeof(input), stdin)) return false;
    str_trim(input);

    bool *selected = (bool *)calloc(list->count, sizeof(bool));
    if (!selected) return false;

    if (!parse_session_selection(input, list->count, selected)) {
        free(selected);
        printf("  " ANSI_YELLOW "Invalid selection." ANSI_RESET "\n\n");
        return false;
    }

    size_t count = 0;
    for (size_t i = 0; i < list->count; i++) {
        if (selected[i]) {
            session_list_add(chosen, &list->items[i]);
            count++;
        }
    }
    free(selected);

    if (count == 0) {
        printf("  " ANSI_DIM "No sessions selected." ANSI_RESET "\n\n");
        return false;
    }

    return true;
}

static int resume_sessions(const session_list_t *list, const terminal_adapter_t *term,
                           bool dry_run, bool open_in_tabs) {
    if (!term) {
        fprintf(stderr, "Error: No suitable terminal client detected or specified.\n");
        return -1;
    }

    /* Filter out sessions that are already running */
    size_t launch_count = 0;
    for (size_t i = 0; i < list->count; i++) {
        if (!list->items[i].is_already_running) {
            launch_count++;
        }
    }

    if (launch_count == 0) {
        printf("  " ANSI_CYAN "All active sessions are already currently running. Nothing to relaunch." ANSI_RESET "\n\n");
        return 0;
    }

    printf(ANSI_BOLD "  Resuming %zu session(s) in %s via %s%s...\n" ANSI_RESET,
           launch_count, open_in_tabs ? "tabs" : "windows",
           term->display_name, dry_run ? " (Dry Run)" : "");

    bool warned_claude = false;
    bool warned_codex = false;
    for (size_t i = 0; i < list->count; i++) {
        const session_t *s = &list->items[i];
        if (s->is_already_running) {
            printf("  " ANSI_DIM "↷ [%s] in %s is already running (skipped)" ANSI_RESET "\n", s->tool_cmd, s->workspace);
            continue;
        }

        if (strcmp(s->tool_cmd, "claude") == 0 && !warned_claude) {
            warned_claude = true;
            if (tool_is_logged_out("claude")) {
                printf("  " ANSI_YELLOW "⚠ claude is not logged in; run `claude auth login` if resume asks for authentication." ANSI_RESET "\n");
            }
        } else if (strcmp(s->tool_cmd, "codex") == 0 && !warned_codex) {
            warned_codex = true;
            if (tool_is_logged_out("codex")) {
                printf("  " ANSI_YELLOW "⚠ codex is not logged in; run `codex login` if resume asks for authentication." ANSI_RESET "\n");
            }
        }

        printf("  " ANSI_CYAN "→" ANSI_RESET " Opening %s for [%s] in %s\n",
               open_in_tabs ? "tab" : "window", s->tool_cmd, s->workspace);
        int res = term->launch_session(s, dry_run, open_in_tabs);
        if (res != 0 && !dry_run) {
            fprintf(stderr, "    " ANSI_RED "Failed to launch %s in %s (exit code %d)\n" ANSI_RESET,
                    open_in_tabs ? "tab" : "window", term->display_name, res);
        }
    }

    printf("\n  " ANSI_BOLD ANSI_GREEN "Session launch complete!" ANSI_RESET "\n\n");
    return 0;
}

int main(int argc, char **argv) {
    flint_options_t opts;
    if (parse_args(argc, argv, &opts) != 0) {
        return 1;
    }

    /* Load plaintext configuration file */
    flint_config_t cfg;
    config_load(opts.config_path, &cfg);

    /* CLI overrides */
    if (opts.lookback_hours > 0) {
        cfg.lookback_hours = opts.lookback_hours;
    }
    if (opts.mcp_focus_override >= 0) {
        cfg.mcp_focus = opts.mcp_focus_override != 0;
    }
    if (opts.wrapper_override[0] != '\0') {
        config_set_wrapper_all(&cfg, opts.wrapper_override);
    }

    if (opts.list_terminals) {
        terminal_print_list();
        return 0;
    }

    if (opts.weekly_summary) {
        return journal_generate_weekly(&cfg);
    }

    if (opts.monthly_summary) {
        return journal_generate_monthly(&cfg);
    }

    if (!opts.json_output) {
        print_banner();
    }

    /* Discover sessions across all registered AI CLI adapters */
    const ai_adapter_t **adapters = NULL;
    size_t adapter_count = ai_get_adapters(&adapters);

    session_list_t all_sessions;
    session_list_init(&all_sessions);

    for (size_t i = 0; i < adapter_count; i++) {
        const ai_adapter_t *ad = adapters[i];
        if (ad->is_available()) {
            session_list_t tool_sessions;
            session_list_init(&tool_sessions);
            ad->discover_sessions(&cfg, cfg.lookback_hours, &tool_sessions);

            for (size_t s = 0; s < tool_sessions.count; s++) {
                const session_t *sess = &tool_sessions.items[s];
                if (opts.workspace_filter[0] != '\0' &&
                    strstr(sess->workspace, opts.workspace_filter) == NULL) {
                    continue;
                }
                session_list_add(&all_sessions, sess);
            }
            session_list_free(&tool_sessions);
        }
    }

    /* Presented overview + focus derived from where work was left */
    brief_t brief;
    memset(&brief, 0, sizeof(brief));
    if (all_sessions.count > 0) {
        brief_build(&all_sessions, &brief);
        brief_collect_scribe_focus(&brief, sizeof(brief.focus) / sizeof(brief.focus[0]));
        if (!opts.json_output) {
            brief_print_overview(&all_sessions, cfg.lookback_hours);
            brief_print_focus(&brief);
        }

        if (cfg.mcp_focus) {
            mcp_target_t target;
            if (mcp_detect_connected(&cfg, &all_sessions, &target)) {
                if (!opts.json_output) {
                    printf(ANSI_DIM "  Consulting connected tools via %s (%s) for today's priorities...\n" ANSI_RESET,
                           target.host_name, target.servers);
                    fflush(stdout);
                }

                char context[16384];
                char mcp_text[8192];
                brief_focus_context(&brief, context, sizeof(context));

                bool mcp_added = mcp_query_focus(&cfg, &target, context, mcp_text, sizeof(mcp_text));

                if (mcp_added) {
                    bool added_new = brief_add_mcp_focus(&brief, mcp_text,
                                                        sizeof(brief.focus) / sizeof(brief.focus[0]),
                                                        target.server_name);
                    if (added_new && !opts.json_output) {
                        brief_print_focus(&brief);
                    }
                } else if (!opts.json_output) {
                    printf(ANSI_DIM "  Connected tools did not return any priorities.\n\n" ANSI_RESET);
                }
            }
        }
    }

    if (opts.json_output) {
        session_list_sort_by_recency(&all_sessions);
        print_json_output(&all_sessions, &brief, &cfg, cfg.lookback_hours);
        session_list_free(&all_sessions);
        config_destroy(&cfg);
        return 0;
    }

    /* Per-session details */
    print_summary(&all_sessions);

    /* Optionally record to markdown Git journal */
    if (opts.write_journal || cfg.auto_journal) {
        journal_record_daily(&cfg, &all_sessions);
    }

    if (all_sessions.count == 0 || opts.summary_only) {
        session_list_free(&all_sessions);
        return 0;
    }

    /* Select target terminal */
    const terminal_adapter_t *target_term = NULL;
    if (opts.target_terminal[0] != '\0') {
        target_term = terminal_find(opts.target_terminal);
    } else if (cfg.preferred_terminal[0] != '\0') {
        target_term = terminal_find(cfg.preferred_terminal);
    }

    if (!target_term) {
        target_term = terminal_detect_default();
    }

    /* Build candidate resume list according to resume_mode */
    session_list_t resume_candidates;
    session_list_init(&resume_candidates);
    for (size_t i = 0; i < all_sessions.count; i++) {
        const session_t *s = &all_sessions.items[i];
        if (!resume_session_should_include(s, cfg.resume_mode)) {
            continue;
        }
        session_list_add(&resume_candidates, s);
    }

    session_list_sort_by_recency(&resume_candidates);

    if (opts.resume_limit > 0) {
        size_t kept = 0;
        for (size_t i = 0; i < resume_candidates.count; i++) {
            if (resume_candidates.items[i].is_already_running) continue;
            if (kept >= (size_t)opts.resume_limit) break;
            resume_candidates.items[kept++] = resume_candidates.items[i];
        }
        resume_candidates.count = kept;
    }

    /* Count sessions that aren't already running */
    size_t resumable_count = 0;
    for (size_t i = 0; i < resume_candidates.count; i++) {
        if (!resume_candidates.items[i].is_already_running) {
            resumable_count++;
        }
    }

    if (resumable_count == 0) {
        printf("  " ANSI_CYAN "All sessions matching resume_mode are already running." ANSI_RESET "\n\n");
        session_list_free(&resume_candidates);
        session_list_free(&all_sessions);
        return 0;
    }

    bool should_resume = opts.resume_now || opts.dry_run;
    session_list_t picked;
    session_list_init(&picked);

    if (!should_resume && isatty(fileno(stdin))) {
        session_list_t pick_list;
        session_list_init(&pick_list);
        for (size_t i = 0; i < resume_candidates.count; i++) {
            if (!resume_candidates.items[i].is_already_running) {
                session_list_add(&pick_list, &resume_candidates.items[i]);
            }
        }

        if (prompt_resume_selection(&pick_list, &picked)) {
            should_resume = true;
        } else {
            printf(ANSI_DIM "  Skipping session resumption.\n\n" ANSI_RESET);
        }

        session_list_free(&pick_list);
    }

    if (should_resume) {
        const session_list_t *to_resume = picked.count > 0 ? &picked : &resume_candidates;
        resume_sessions(to_resume, target_term, opts.dry_run, cfg.open_in_tabs);
    }

    session_list_free(&picked);
    session_list_free(&resume_candidates);
    session_list_free(&all_sessions);
    return 0;
}

#endif
