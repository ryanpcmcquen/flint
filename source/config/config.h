#ifndef FLINT_CONFIG_H
#define FLINT_CONFIG_H

#include "../flint.h"

/* Initialize config struct with sensible defaults */
void config_init_defaults(flint_config_t *cfg);

/* Load plaintext config file from disk. If path is NULL, checks ~/.config/flint/config and ~/.flintrc */
bool config_load(const char *custom_path, flint_config_t *cfg);

/* Write default template config file if none exists */
bool config_create_default_file(const char *path);

/* Get configured tool settings */
const tool_config_t *config_get_tool(const flint_config_t *cfg, const char *tool_cmd);

/* Format resume command using wrapper, model, and effort */
void config_format_tool_cmd(const flint_config_t *cfg, const char *tool_cmd,
                            const char *session_id, char *out_cmd, size_t out_size);

#endif /* FLINT_CONFIG_H */
