#ifndef FLINT_AI_MCP_H
#define FLINT_AI_MCP_H

#include "../flint.h"

typedef struct {
    char servers[512];
    char server_name[MAX_NAME_LEN];
    char workspace[MAX_PATH_LEN];
} mcp_target_t;

/* Parse an MCP list response and collect the names of connected servers. */
bool mcp_parse_connected_servers(const char *json, char *out, size_t out_size);

/* Find a workspace with at least one connected MCP server. */
bool mcp_detect_connected(const session_list_t *list, mcp_target_t *out);

/* Ask a read-only agent to combine recent work with connected MCP data. */
bool mcp_query_focus(const flint_config_t *cfg, const mcp_target_t *target,
                     const char *focus_context, char *out, size_t out_size);

#endif /* FLINT_AI_MCP_H */
