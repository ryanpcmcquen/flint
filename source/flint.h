#ifndef FLINT_H
#define FLINT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#define FLINT_VERSION "0.1.0"
#define MAX_PATH_LEN 1024
#define MAX_CMD_LEN 1024
#define MAX_NAME_LEN 64
#define MAX_SUMMARY_LEN 2048
#define MAX_ITEMS 64

/* ANSI styling */
#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_DIM     "\033[2m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_BLUE    "\033[34m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_RED     "\033[31m"

/* Session representation */
typedef struct {
    char id[MAX_NAME_LEN];
    char tool_name[MAX_NAME_LEN];   /* e.g., "Antigravity CLI", "Claude Code", "OpenAI Codex" */
    char tool_cmd[MAX_NAME_LEN];    /* e.g., "agy", "claude", "codex" */
    char workspace[MAX_PATH_LEN];   /* Project working directory */
    time_t last_active;             /* Timestamp of last activity */
    char last_active_str[64];       /* Human-readable timestamp */
    char summary[MAX_SUMMARY_LEN];  /* Accomplishment summary */
    char resume_cmd[MAX_CMD_LEN];   /* Shell command to resume */
} session_t;

/* List of discovered sessions */
typedef struct {
    session_t *items;
    size_t count;
    size_t capacity;
} session_list_t;

/* CLI options */
typedef struct {
    bool summary_only;
    bool resume_now;
    bool dry_run;
    int lookback_hours;
    char target_terminal[MAX_NAME_LEN];
    char workspace_filter[MAX_PATH_LEN];
    bool list_terminals;
    bool verbose;
} flint_options_t;

/* Session list management functions */
void session_list_init(session_list_t *list);
void session_list_add(session_list_t *list, const session_t *session);
void session_list_free(session_list_t *list);

#endif /* FLINT_H */
