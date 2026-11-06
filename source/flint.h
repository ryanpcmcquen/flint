#ifndef FLINT_H
#define FLINT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#define FLINT_VERSION "0.2.1"
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
    /* Directly created/prompted by the human user */
    SESSION_ORIGIN_USER = 0,
    /* Created by an agent/subagent or background automation */
    SESSION_ORIGIN_AGENT = 1
} session_origin_t;

/* Resume filter modes */
typedef enum {
    /* Only user-created sessions (default) */
    RESUME_MODE_USER = 0,
    /* Both user and agent-created sessions */
    RESUME_MODE_ALL = 1,
    /* Only agent-created sessions */
    RESUME_MODE_AGENT = 2
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
    /* e.g., "Antigravity CLI", "Claude Code", "ChatGPT Codex" */
    char tool_name[MAX_NAME_LEN];
    /* e.g., "agy", "claude", "codex" */
    char tool_cmd[MAX_NAME_LEN];
    /* Project working directory */
    char workspace[MAX_PATH_LEN];
    /* Timestamp of last activity or access */
    time_t last_active;
    /* Human-readable timestamp */
    char last_active_str[64];
    /* Accomplishment summary */
    char summary[MAX_SUMMARY_LEN];
    /* Full command to resume, with wrapper & flags */
    char resume_cmd[MAX_CMD_LEN];
    /* USER or AGENT */
    session_origin_t origin;
    /* Open yesterday without completed prompts */
    bool was_merely_open;
    /* Process is currently running */
    bool is_already_running;
    /* Git status for the workspace */
    git_info_t git;
} session_t;

/* List of discovered sessions */
typedef struct {
    session_t *items;
    size_t count;
    size_t capacity;
} session_list_t;

/* Per-tool configuration options */
typedef struct {
    /* e.g., "agy", "claude", "codex" */
    char name[MAX_NAME_LEN];
    /* e.g., "graperoot" or empty */
    char wrapper[MAX_NAME_LEN];
    /* e.g., "gemini-3.8-flash", "claude-3-7-sonnet" */
    char model[MAX_NAME_LEN];
    /* e.g., "high", "medium", "low" */
    char effort[MAX_NAME_LEN];
} tool_config_t;

/* Plaintext Flint configuration (~/.config/flint/config) */
typedef struct {
    char preferred_terminal[MAX_NAME_LEN];
    resume_mode_t resume_mode;
    char journal_repo[MAX_PATH_LEN];
    int lookback_hours;
    bool open_in_tabs;
    bool auto_journal;
    /* Query connected MCP tools for today's focus */
    bool mcp_focus;
    /* Preferred MCP host: auto (blank), agy, claude, cline, codex, opencode, openclaw */
    char mcp_host[MAX_NAME_LEN];

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
    /* --wrapper: prepend to every resume command */
    char wrapper_override[MAX_NAME_LEN];
    char workspace_filter[MAX_PATH_LEN];
    char config_path[MAX_PATH_LEN];
    bool list_terminals;
    bool write_journal;
    bool weekly_summary;
    bool monthly_summary;
    /* -1 = unset, 0 = off, 1 = on */
    int mcp_focus_override;
} flint_options_t;

/* Session list management functions */
void session_list_init(session_list_t *list);
void session_list_add(session_list_t *list, const session_t *session);
void session_list_free(session_list_t *list);

/* Whether a session should be included given the resume mode filter */
bool resume_session_should_include(const session_t *s, resume_mode_t mode);

#endif
