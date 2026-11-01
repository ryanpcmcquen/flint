#ifndef FLINT_CONFIG_H
#define FLINT_CONFIG_H

#include "../flint.h"

/* Initialize config struct with sensible defaults */
void config_init_defaults(flint_config_t *cfg);

/* Load plaintext config file from disk. If path is NULL, checks ~/.config/flint/config and ~/.flintrc */
bool config_load(const char *custom_path, flint_config_t *cfg);

/* Write default template config file if none exists */
bool config_create_default_file(const char *path);

/* Free a config struct (including its tool list) */
void config_destroy(flint_config_t *cfg);

/* Find the configured tool settings for a tool command (e.g. "agy"). Returns
 * NULL if the tool is not configured. */
const tool_config_t *config_find_tool(const flint_config_t *cfg, const char *tool_cmd);

/* Append a tool configuration to the config struct's tool list. Used for
 * defaults and for tests. */
void config_add_tool(flint_config_t *cfg, const char *name, const char *wrapper,
                     const char *model, const char *effort);

/* Format resume command using wrapper, model, and effort.
 * tool_cmd is the adapter binary name (e.g. "agy", "claude", "codex"). */
void config_format_tool_cmd(const flint_config_t *cfg, const char *tool_cmd,
                            const char *session_id, char *out_cmd, size_t out_size);

#endif /* FLINT_CONFIG_H */
