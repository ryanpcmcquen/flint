#ifndef FLINT_H
#define FLINT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#define FLINT_VERSION "0.2.0"
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

/* Session creation origin */
typedef enum {
    SESSION_ORIGIN_USER = 0,    /* Directly created/prompted by the human user */
    SESSION_ORIGIN_AGENT = 1    /* Created by an agent/subagent or background automation */
} session_origin_t;

/* Resume filter modes */
typedef enum {
    RESUME_MODE_USER = 0,       /* Only user-created sessions (default) */
    RESUME_MODE_ALL = 1,        /* Both user and agent-created sessions */
    RESUME_MODE_AGENT = 2       /* Only agent-created sessions */
} resume_mode_t;

/* Local Git Activity */
typedef struct {
    bool is_git_repo;
    char branch[MAX_NAME_LEN];
    int uncommitted_count;
    char uncommitted_summary[MAX_SUMMARY_LEN];
    int commit_count;
    char recent_commits[MAX_SUMMARY_LEN];
    char suggested_focus[MAX_SUMMARY_LEN];
} git_info_t;

/* Session representation */
typedef struct {
    char id[MAX_NAME_LEN];
    char tool_name[MAX_NAME_LEN];       /* e.g., "Antigravity CLI", "Claude Code", "ChatGPT Codex" */
    char tool_cmd[MAX_NAME_LEN];        /* e.g., "agy", "claude", "codex" */
    char workspace[MAX_PATH_LEN];       /* Project working directory */
    time_t last_active;                 /* Timestamp of last activity or access */
    char last_active_str[64];           /* Human-readable timestamp */
    char summary[MAX_SUMMARY_LEN];      /* Accomplishment summary */
    char resume_cmd[MAX_CMD_LEN];       /* Full command to resume, with wrapper & flags */
    session_origin_t origin;            /* USER or AGENT */
    bool was_merely_open;               /* Open yesterday without completed prompts */
    bool is_already_running;            /* Process is currently running */
    git_info_t git;                     /* Git status for the workspace */
} session_t;

/* List of discovered sessions */
typedef struct {
    session_t *items;
    size_t count;
    size_t capacity;
} session_list_t;

/* Per-tool configuration options */
typedef struct {
    char name[MAX_NAME_LEN];            /* e.g., "agy", "claude", "codex" */
    char wrapper[MAX_NAME_LEN];         /* e.g., "graperoot" or empty */
    char model[MAX_NAME_LEN];           /* e.g., "gemini-3.8-flash", "claude-3-7-sonnet" */
    char effort[MAX_NAME_LEN];          /* e.g., "high", "medium", "low" */
} tool_config_t;

/* Plaintext Flint configuration (~/.config/flint/config) */
typedef struct {
    char preferred_terminal[MAX_NAME_LEN];
    resume_mode_t resume_mode;
    char journal_repo[MAX_PATH_LEN];
    int lookback_hours;
    bool open_in_tabs;
    bool auto_journal;
    bool mcp_focus;                     /* Query connected MCP tools for today's focus */

    /* Ordered list of tool configurations. Each AI CLI adapter owns one
     * entry; anything not listed gets no wrapper by default. */
    tool_config_t *tool_configs;
    size_t tool_config_count;
    size_t tool_config_capacity;
} flint_config_t;

/* CLI runtime options */
typedef struct {
    bool summary_only;
    bool resume_now;
    bool dry_run;
    int lookback_hours;
    char target_terminal[MAX_NAME_LEN];
    char workspace_filter[MAX_PATH_LEN];
    char config_path[MAX_PATH_LEN];
    bool list_terminals;
    bool write_journal;
    bool weekly_summary;
    bool monthly_summary;
    bool verbose;
    int mcp_focus_override;             /* -1 = unset, 0 = off, 1 = on */
} flint_options_t;

/* Session list management functions */
void session_list_init(session_list_t *list);
void session_list_add(session_list_t *list, const session_t *session);
void session_list_free(session_list_t *list);

#endif /* FLINT_H */
