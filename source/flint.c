#include "flint.h"
#include "ai/ai_adapter.h"
#include "terminal/terminal.h"
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
    printf("  -t, --terminal <name>   Target terminal client (e.g. iterm2, ghostty, apple-terminal,\n");
    printf("                          gnome-terminal, alacritty, kitty, windows-terminal)\n");
    printf("  -d, --days <N>          Look back N days (default: 1 day / 24 hours)\n");
    printf("      --hours <H>         Look back H hours\n");
    printf("  -w, --workspace <path>  Filter sessions to specific workspace directory\n");
    printf("  -n, --dry-run           Show commands that would be executed without launching\n");
    printf("  -l, --list-terminals    List all supported and detected terminal clients\n");
    printf("  -v, --version           Print Flint version\n");
    printf("  -h, --help              Print this help menu\n\n");
}

static int parse_args(int argc, char **argv, flint_options_t *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->lookback_hours = 24;

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
        } else if (strcmp(arg, "-l") == 0 || strcmp(arg, "--list-terminals") == 0) {
            opts->list_terminals = true;
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
        printf("\n  " ANSI_BOLD ANSI_CYAN "[%s]" ANSI_RESET " %s\n", s->tool_name, s->workspace);
        printf("    " ANSI_DIM "Last active:" ANSI_RESET " %s\n", s->last_active_str);
        printf("    " ANSI_BOLD ANSI_GREEN "✓ " ANSI_RESET "%s\n", s->summary);
        printf("    " ANSI_DIM "Resume command:" ANSI_RESET " cd \"%s\" && %s\n", s->workspace, s->resume_cmd);
    }

    printf("\n  " ANSI_DIM "────────────────────────────────────────────────────────────" ANSI_RESET "\n\n");
}

static int resume_sessions(const session_list_t *list, const terminal_adapter_t *term, bool dry_run) {
    if (!term) {
        fprintf(stderr, "Error: No suitable terminal client detected or specified.\n");
        return -1;
    }

    printf(ANSI_BOLD "  Resuming %zu session(s) in %s%s...\n" ANSI_RESET,
           list->count, term->display_name, dry_run ? " (Dry Run)" : "");

    for (size_t i = 0; i < list->count; i++) {
        const session_t *s = &list->items[i];
        printf("  " ANSI_CYAN "→" ANSI_RESET " [%s] in %s\n", s->tool_cmd, s->workspace);
        int res = term->launch_session(s, dry_run);
        if (res != 0 && !dry_run) {
            fprintf(stderr, "    " ANSI_RED "Failed to launch session in %s (exit code %d)\n" ANSI_RESET, term->display_name, res);
        }
    }

    printf("\n  " ANSI_BOLD ANSI_GREEN "All active sessions resumed!" ANSI_RESET "\n\n");
    return 0;
}

int main(int argc, char **argv) {
    flint_options_t opts;
    if (parse_args(argc, argv, &opts) != 0) {
        return 1;
    }

    if (opts.list_terminals) {
        terminal_print_list();
        return 0;
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
            ad->discover_sessions(opts.lookback_hours, &tool_sessions);

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

    if (all_sessions.count == 0 || opts.summary_only) {
        session_list_free(&all_sessions);
        return 0;
    }

    /* Select target terminal */
    const terminal_adapter_t *target_term = NULL;
    if (opts.target_terminal[0] != '\0') {
        target_term = terminal_find(opts.target_terminal);
        if (!target_term) {
            fprintf(stderr, ANSI_RED "Unknown terminal '%s'. Use --list-terminals to view options.\n" ANSI_RESET, opts.target_terminal);
            session_list_free(&all_sessions);
            return 1;
        }
    } else {
        target_term = terminal_detect_default();
    }

    bool should_resume = opts.resume_now || opts.dry_run;

    if (!should_resume && isatty(fileno(stdin))) {
        printf("  Resume %zu active session(s) in " ANSI_BOLD "%s" ANSI_RESET "? [Y/n] ",
               all_sessions.count, target_term ? target_term->display_name : "terminal");
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
        resume_sessions(&all_sessions, target_term, opts.dry_run);
    }

    session_list_free(&all_sessions);
    return 0;
}
#endif
