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

#ifdef TEST_MODE
/* Test-only hook: run one Antigravity transcript line through the parser. */
int flint_agy_parse_line(const char *line, size_t len, void *ctx);
#endif

#endif /* FLINT_AI_ADAPTER_H */
