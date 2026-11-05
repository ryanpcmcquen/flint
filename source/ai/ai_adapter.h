#ifndef FLINT_AI_ADAPTER_H
#define FLINT_AI_ADAPTER_H

#include "../flint.h"

typedef struct ai_adapter {
    const char *name;
    const char *cli_binary;
    bool (*is_available)(void);
    int (*discover_sessions)(const flint_config_t *cfg, int lookback_hours, session_list_t *list);
} ai_adapter_t;

/* Available AI adapters */
extern const ai_adapter_t antigravity_adapter;
extern const ai_adapter_t claude_adapter;
extern const ai_adapter_t codex_adapter;
extern const ai_adapter_t cline_adapter;
extern const ai_adapter_t opencode_adapter;
extern const ai_adapter_t openclaw_adapter;

/* Scan context threaded through Antigravity transcript line parsing. */
typedef struct {
    char session_id[MAX_NAME_LEN];
    char workspace[MAX_PATH_LEN];
    char last_user_req[MAX_SUMMARY_LEN];
    char accomplishments[MAX_SUMMARY_LEN];
    time_t last_time;
    int prompt_count;
    session_origin_t origin;
    /* Scratch buffer for jsonl line parsing (reused, avoids per-line malloc) */
    char line_buf[1024];
} agy_scan_ctx_t;

/* Master registry */
size_t ai_get_adapters(const ai_adapter_t ***adapters);

/* Parsed metadata from the head of a Claude Code transcript
 * (~/.claude/projects/<project>/<session-id>.jsonl). */
typedef struct {
    char session_id[MAX_NAME_LEN];
    char workspace[MAX_PATH_LEN];
    char summary[MAX_SUMMARY_LEN];
    bool is_sidechain;
} claude_head_t;

/* Parsed metadata from the head of a Codex rollout
 * (~/.codex/sessions/YYYY/MM/DD/rollout-*.jsonl). */
typedef struct {
    char session_id[MAX_NAME_LEN];
    char workspace[MAX_PATH_LEN];
    char summary[MAX_SUMMARY_LEN];
    char git_branch[MAX_NAME_LEN];
    bool is_agent;
} codex_head_t;

/* Parse the first chunk of a transcript/rollout. Both are tolerant of
 * truncated trailing lines and return false when no useful metadata is found. */
bool flint_claude_parse_head(const char *jsonl, claude_head_t *out);
bool flint_codex_parse_head(const char *jsonl, codex_head_t *out);

#ifdef TEST_MODE
/* Test-only hook: run one Antigravity transcript line through the parser. */
int flint_agy_parse_line(const char *line, size_t len, void *ctx);

/* Test-only hook: parse an OpenCode session.list response. */
int flint_opencode_parse_json(const char *json, const flint_config_t *cfg,
                              int lookback_hours, session_list_t *list);
#endif

#endif /* FLINT_AI_ADAPTER_H */
