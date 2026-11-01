#include "flint.h"
#include "ai/ai_adapter.h"
#include "terminal/terminal.h"
#include "config/config.h"
#include "git/git.h"
#include "journal/journal.h"
#include "process/proc.h"
#include "util/fs.h"

#ifdef _WIN32
  #include <io.h>
  #define isatty _isatty
  #define fileno _fileno
#else
  #include <unistd.h>
#endif

void session_list_init(session_list_t *list) {
    if (!list) return;
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void session_list_add(session_list_t *list, const session_t *session) {
    if (!list || !session) return;

    /* Deduplicate: if workspace and tool match, update to the most recent */
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].workspace, session->workspace) == 0 &&
            strcmp(list->items[i].tool_cmd, session->tool_cmd) == 0) {
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

void session_list_free(session_list_t *list) {
    if (!list) return;
    if (list->items) {
        free(list->items);
        list->items = NULL;
    }
    list->count = 0;
    list->capacity = 0;
}

#ifndef TEST_MODE

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
    printf(ANSI_BOLD ANSI_CYAN "  ║                        " ANSI_YELLOW "F L I N T" ANSI_CYAN "                             ║\n" ANSI_RESET);
    printf(ANSI_BOLD ANSI_CYAN "  ║            " ANSI_RESET "Morning Standup Brief & AI Session Manager" ANSI_BOLD ANSI_CYAN "        ║\n" ANSI_RESET);
    printf(ANSI_BOLD ANSI_CYAN "  ║                     " ANSI_DIM "%-24s" ANSI_RESET ANSI_BOLD ANSI_CYAN "         ║\n" ANSI_RESET, date_buf);
    printf(ANSI_BOLD ANSI_CYAN "  ╚══════════════════════════════════════════════════════════════╝\n\n" ANSI_RESET);
}

static void print_usage(const char *prog_name) {
    printf(ANSI_BOLD "Usage:" ANSI_RESET " %s [options]\n\n", prog_name);
    printf(ANSI_BOLD "Options:\n" ANSI_RESET);
    printf("  -s, --summary           Display daily summary only (do not launch sessions)\n");
    printf("  -r, --resume            Automatically resume all active sessions without prompting\n");
    printf("  -t, --terminal <name>   Target terminal client (iterm2, ghostty, kitty, apple-terminal,\n");
    printf("                          gnome-terminal, alacritty, konsole, windows-terminal, powershell)\n");
    printf("  -d, --days <N>          Look back N days (default: 1 day / 24 hours)\n");
    printf("      --hours <H>         Look back H hours\n");
    printf("  -w, --workspace <path>  Filter sessions to specific workspace directory\n");
    printf("      --origin <mode>     Filter by session origin: 'user', 'agent', or 'all'\n");
    printf("  -j, --journal           Record daily standup log to markdown Git repository\n");
    printf("      --weekly            Generate and display weekly standup summary\n");
    printf("      --monthly           Generate and display monthly standup summary\n");
    printf("  -c, --config <path>     Path to custom flint configuration file\n");
    printf("      --init-config       Generate default ~/.config/flint/config template\n");
    printf("  -n, --dry-run           Show commands that would be executed without launching\n");
    printf("  -l, --list-terminals    List all supported and detected terminal clients\n");
    printf("  -v, --version           Print Flint version\n");
    printf("  -h, --help              Print this help menu\n\n");
}

static int parse_args(int argc, char **argv, flint_options_t *opts) {
    memset(opts, 0, sizeof(*opts));

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
        } else if (strcmp(arg, "-l") == 0 || strcmp(arg, "--list-terminals") == 0) {
            opts->list_terminals = true;
        } else if (strcmp(arg, "--init-config") == 0) {
            char cfg_path[MAX_PATH_LEN];
            fs_expand_path("~/.config/flint/config", cfg_path, sizeof(cfg_path));
            char cfg_dir[MAX_PATH_LEN];
            fs_expand_path("~/.config/flint", cfg_dir, sizeof(cfg_dir));
            char mkdir_cmd[MAX_PATH_LEN * 2];
            snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p \"%s\" 2>/dev/null", cfg_dir);
            system(mkdir_cmd);

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
        } else if ((strcmp(arg, "-d") == 0 || strcmp(arg, "--days") == 0) && i + 1 < argc) {
            int days = atoi(argv[++i]);
            if (days > 0) opts->lookback_hours = days * 24;
        } else if (strcmp(arg, "--hours") == 0 && i + 1 < argc) {
            int hours = atoi(argv[++i]);
            if (hours > 0) opts->lookback_hours = hours;
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

    printf(ANSI_BOLD "  What was worked on & accomplished:\n" ANSI_RESET);
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
            if (s->git.suggested_focus[0] != '\0') {
                printf("    " ANSI_BOLD ANSI_YELLOW "💡 Focus:" ANSI_RESET " %s\n", s->git.suggested_focus);
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

static int resume_sessions(const session_list_t *list, const terminal_adapter_t *term, bool dry_run) {
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

    printf(ANSI_BOLD "  Resuming %zu session(s) in tabs via %s%s...\n" ANSI_RESET,
           launch_count, term->display_name, dry_run ? " (Dry Run)" : "");

    for (size_t i = 0; i < list->count; i++) {
        const session_t *s = &list->items[i];
        if (s->is_already_running) {
            printf("  " ANSI_DIM "↷ [%s] in %s is already running (skipped)" ANSI_RESET "\n", s->tool_cmd, s->workspace);
            continue;
        }

        printf("  " ANSI_CYAN "→" ANSI_RESET " Opening tab for [%s] in %s\n", s->tool_cmd, s->workspace);
        int res = term->launch_session(s, dry_run);
        if (res != 0 && !dry_run) {
            fprintf(stderr, "    " ANSI_RED "Failed to launch tab in %s (exit code %d)\n" ANSI_RESET, term->display_name, res);
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

    print_banner();

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

    /* Print daily standup summary */
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
        if (cfg.resume_mode == RESUME_MODE_USER && s->origin != SESSION_ORIGIN_USER) {
            continue;
        }
        if (cfg.resume_mode == RESUME_MODE_AGENT && s->origin != SESSION_ORIGIN_AGENT) {
            continue;
        }
        session_list_add(&resume_candidates, s);
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

    if (!should_resume && isatty(fileno(stdin))) {
        const char *mode_str = (cfg.resume_mode == RESUME_MODE_USER) ? "user" :
                               (cfg.resume_mode == RESUME_MODE_AGENT) ? "agent" : "all";
        printf("  Resume %zu %s session(s) in tabs via " ANSI_BOLD "%s" ANSI_RESET "? [Y/n] ",
               resumable_count, mode_str, target_term ? target_term->display_name : "terminal");
        fflush(stdout);

        char input[32] = {0};
        if (fgets(input, sizeof(input), stdin)) {
            str_trim(input);
            if (input[0] == '\0' || input[0] == 'y' || input[0] == 'Y') {
                should_resume = true;
            } else {
                printf(ANSI_DIM "  Skipping session resumption.\n\n" ANSI_RESET);
            }
        }
    }

    if (should_resume) {
        resume_sessions(&resume_candidates, target_term, opts.dry_run);
    }

    session_list_free(&resume_candidates);
    session_list_free(&all_sessions);
    return 0;
}

#endif
