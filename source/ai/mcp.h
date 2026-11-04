#ifndef FLINT_AI_MCP_H
#define FLINT_AI_MCP_H

#include "../flint.h"

typedef struct {
    char servers[512];
    char server_name[MAX_NAME_LEN];
    char workspace[MAX_PATH_LEN];
} mcp_target_t;

/* Mutable accumulator for one MCP run's collected text (internal to mcp.c). */
typedef struct {
    char text[8192];
    size_t len;
    char session_id[MAX_NAME_LEN];
} mcp_run_t;

#ifdef TEST_MODE
/* Test-only hook: run one MCP JSONL line through the collector. */
int flint_mcp_collect_line(const char *line, size_t len, void *ctx);
#endif

/* Parse an MCP list response and collect the names of connected servers. */
bool mcp_parse_connected_servers(const char *json, char *out, size_t out_size);

/* Find a workspace with at least one connected MCP server. */
bool mcp_detect_connected(const session_list_t *list, mcp_target_t *out);

/* Ask a read-only agent to combine recent work with connected MCP data. */
bool mcp_query_focus(const flint_config_t *cfg, const mcp_target_t *target,
                     const char *focus_context, char *out, size_t out_size);

/* Ask a read-only agent to combine recent work with connected MCP data,
 * returning the session id used (may be NULL). */
bool mcp_query_prompt(const char *workspace, const char *context, const char *servers,
                     const char *instructions, char *out, size_t out_size, char *session_id_out);

#endif /* FLINT_AI_MCP_H */
