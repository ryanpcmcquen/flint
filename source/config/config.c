#include "config.h"
#include "../util/fs.h"
#include <ctype.h>
#include <string.h>

void config_init_defaults(flint_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));

    strncpy(cfg->preferred_terminal, "", sizeof(cfg->preferred_terminal) - 1);
    cfg->resume_mode = RESUME_MODE_USER;
    fs_expand_path("~/notes/flint-journal", cfg->journal_repo, sizeof(cfg->journal_repo));
    cfg->lookback_hours = 24;
    cfg->open_in_tabs = true;
    cfg->auto_journal = false;
    cfg->mcp_focus = true;

    /* Tool defaults.
     *
     * By default there is no wrapper binary: leave the `wrapper` field empty
     * and Flint just runs the raw CLI. Use a `wrapper = <name>` entry under
     * the tool's section in the config file to prepend a launcher like
     * GrapeRoot (which normally wraps `agy`) around the command. */
    config_add_tool(cfg, "agy", "", "gemini-3.8-flash", "high");
    config_add_tool(cfg, "claude", "", "claude-3-7-sonnet", "high");
    config_add_tool(cfg, "codex", "", "o3-mini", "high");
    config_add_tool(cfg, "cline", "", "", "");
    config_add_tool(cfg, "opencode", "", "", "");
}

void config_add_tool(flint_config_t *cfg, const char *name, const char *wrapper,
                     const char *model, const char *effort) {
    if (!cfg || !name) return;

    if (cfg->tool_config_count >= cfg->tool_config_capacity) {
        size_t new_cap = cfg->tool_config_capacity ? cfg->tool_config_capacity * 2 : 8;
        tool_config_t *new_items = (tool_config_t *)realloc(cfg->tool_configs,
                                                            new_cap * sizeof(*new_items));
        if (!new_items) return;
        cfg->tool_configs = new_items;
        cfg->tool_config_capacity = new_cap;
    }

    tool_config_t *tc = &cfg->tool_configs[cfg->tool_config_count++];
    memset(tc, 0, sizeof(*tc));
    strncpy(tc->name, name, sizeof(tc->name) - 1);
    strncpy(tc->wrapper, wrapper, sizeof(tc->wrapper) - 1);
    strncpy(tc->model, model, sizeof(tc->model) - 1);
    strncpy(tc->effort, effort, sizeof(tc->effort) - 1);
}

const tool_config_t *config_find_tool(const flint_config_t *cfg, const char *tool_cmd) {
    if (!cfg || !tool_cmd) return NULL;
    for (size_t i = 0; i < cfg->tool_config_count; i++) {
        if (strcmp(cfg->tool_configs[i].name, tool_cmd) == 0) {
            return &cfg->tool_configs[i];
        }
    }
    return NULL;
}

void config_destroy(flint_config_t *cfg) {
    if (!cfg) return;
    if (cfg->tool_configs) {
        free(cfg->tool_configs);
        cfg->tool_configs = NULL;
        cfg->tool_config_count = 0;
        cfg->tool_config_capacity = 0;
    }
}

void config_format_tool_cmd(const flint_config_t *cfg, const char *tool_cmd,
                            const char *session_id, char *out_cmd, size_t out_size) {
    if (!out_cmd || out_size == 0) return;
    out_cmd[0] = '\0';

    const tool_config_t *tc = config_find_tool(cfg, tool_cmd);
    char prefix[MAX_NAME_LEN * 2] = {0};
    if (tc && tc->wrapper[0] != '\0') {
        snprintf(prefix, sizeof(prefix), "%s ", tc->wrapper);
    }

    if (strcmp(tool_cmd, "agy") == 0) {
        char flags[256] = {0};
        if (tc && tc->model[0] != '\0') {
            snprintf(flags + strlen(flags), sizeof(flags) - strlen(flags), " --model %s", tc->model);
        }
        if (tc && tc->effort[0] != '\0') {
            snprintf(flags + strlen(flags), sizeof(flags) - strlen(flags), " --effort %s", tc->effort);
        }
        snprintf(out_cmd, out_size, "%sagy%s", prefix, flags);
    } else if (strcmp(tool_cmd, "claude") == 0) {
        char flags[256] = {0};
        if (tc && tc->model[0] != '\0') {
            snprintf(flags + strlen(flags), sizeof(flags) - strlen(flags), " --model %s", tc->model);
        }
        snprintf(out_cmd, out_size, "%sclaude%s", prefix, flags);
    } else if (strcmp(tool_cmd, "codex") == 0) {
        char flags[256] = {0};
        if (tc && tc->model[0] != '\0') {
            snprintf(flags + strlen(flags), sizeof(flags) - strlen(flags), " --model %s", tc->model);
        }
        snprintf(out_cmd, out_size, "%scodex%s", prefix, flags);
    } else if (strcmp(tool_cmd, "cline") == 0) {
        char flags[512] = {0};
        if (session_id && session_id[0] != '\0') {
            snprintf(flags + strlen(flags), sizeof(flags) - strlen(flags), " --id %s", session_id);
        }
        if (tc && tc->model[0] != '\0') {
            snprintf(flags + strlen(flags), sizeof(flags) - strlen(flags), " --model %s", tc->model);
        }
        if (tc && tc->effort[0] != '\0') {
            snprintf(flags + strlen(flags), sizeof(flags) - strlen(flags), " --thinking %s", tc->effort);
        }
        snprintf(out_cmd, out_size, "%scline%s", prefix, flags);
    } else if (strcmp(tool_cmd, "opencode") == 0) {
        char flags[512] = {0};
        if (session_id && session_id[0] != '\0') {
            snprintf(flags + strlen(flags), sizeof(flags) - strlen(flags), " --session %s", session_id);
        }
        snprintf(out_cmd, out_size, "%sopencode%s", prefix, flags);
    } else {
        snprintf(out_cmd, out_size, "%s%s", prefix, tool_cmd);
    }
}

static const char *tool_name_for_section(const char *section) {
    if (strcasecmp(section, "antigravity") == 0 || strcasecmp(section, "agy") == 0) {
        return "agy";
    }
    if (strcasecmp(section, "claude") == 0) {
        return "claude";
    }
    if (strcasecmp(section, "codex") == 0 || strcasecmp(section, "openai") == 0) {
        return "codex";
    }
    if (strcasecmp(section, "cline") == 0) {
        return "cline";
    }
    if (strcasecmp(section, "opencode") == 0 || strcasecmp(section, "open-code") == 0) {
        return "opencode";
    }
    return NULL;
}

static void parse_line(char *line, char *current_section, size_t section_size, flint_config_t *cfg) {
    str_trim(line);
    if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
        return;
    }

    /* Section header [section] */
    if (line[0] == '[') {
        char *end = strchr(line, ']');
        if (end) {
            *end = '\0';
            strncpy(current_section, line + 1, section_size - 1);
            current_section[section_size - 1] = '\0';
            str_trim(current_section);
        }
        return;
    }

    /* Key = Value */
    char *eq = strchr(line, '=');
    if (!eq) eq = strchr(line, ':');
    if (!eq) return;

    *eq = '\0';
    char *key = line;
    char *val = eq + 1;
    str_trim(key);
    str_trim(val);

    /* Top-level global keys */
    if (current_section[0] == '\0') {
        if (strcasecmp(key, "terminal") == 0) {
            strncpy(cfg->preferred_terminal, val, sizeof(cfg->preferred_terminal) - 1);
        } else if (strcasecmp(key, "resume_mode") == 0) {
            if (strcasecmp(val, "all") == 0) {
                cfg->resume_mode = RESUME_MODE_ALL;
            } else if (strcasecmp(val, "agent") == 0 || strcasecmp(val, "agents") == 0) {
                cfg->resume_mode = RESUME_MODE_AGENT;
            } else {
                cfg->resume_mode = RESUME_MODE_USER;
            }
        } else if (strcasecmp(key, "journal_repo") == 0 || strcasecmp(key, "journal_path") == 0) {
            fs_expand_path(val, cfg->journal_repo, sizeof(cfg->journal_repo));
        } else if (strcasecmp(key, "lookback_hours") == 0) {
            int h = atoi(val);
            if (h > 0) cfg->lookback_hours = h;
        } else if (strcasecmp(key, "open_in_tabs") == 0) {
            cfg->open_in_tabs = (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0 || strcasecmp(val, "yes") == 0);
        } else if (strcasecmp(key, "auto_journal") == 0) {
            cfg->auto_journal = (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0 || strcasecmp(val, "yes") == 0);
        } else if (strcasecmp(key, "mcp_focus") == 0) {
            cfg->mcp_focus = (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0 || strcasecmp(val, "yes") == 0);
        }
        return;
    }

    /* Tool section: [agy], [claude], [codex], or human-friendly aliases */
    const char *tool = tool_name_for_section(current_section);
    if (!tool) return;

    tool_config_t *tc = (tool_config_t *)config_find_tool(cfg, tool);
    if (!tc) return;

    if (strcasecmp(key, "wrapper") == 0) {
        strncpy(tc->wrapper, val, sizeof(tc->wrapper) - 1);
    } else if (strcasecmp(key, "model") == 0) {
        strncpy(tc->model, val, sizeof(tc->model) - 1);
    } else if (strcasecmp(key, "effort") == 0) {
        strncpy(tc->effort, val, sizeof(tc->effort) - 1);
    }
}

bool config_load(const char *custom_path, flint_config_t *cfg) {
    if (!cfg) return false;
    config_init_defaults(cfg);

    char path[MAX_PATH_LEN] = {0};
    if (custom_path && custom_path[0] != '\0') {
        fs_expand_path(custom_path, path, sizeof(path));
    } else {
        char try1[MAX_PATH_LEN];
        char try2[MAX_PATH_LEN];
        fs_expand_path("~/.config/flint/config", try1, sizeof(try1));
        fs_expand_path("~/.flintrc", try2, sizeof(try2));

        if (fs_file_exists(try1)) {
            strncpy(path, try1, sizeof(path) - 1);
        } else if (fs_file_exists(try2)) {
            strncpy(path, try2, sizeof(path) - 1);
        } else {
            /* No file found, use defaults */
            return false;
        }
    }

    size_t size = 0;
    char *content = fs_read_file(path, &size);
    if (!content) return false;

    char current_section[MAX_NAME_LEN] = {0};
    char *line = content;
    while (*line) {
        char *line_start = line;
        while (*line && *line != '\n' && *line != '\r') line++;

        char saved = *line;
        *line = '\0';
        parse_line(line_start, current_section, sizeof(current_section), cfg);
        *line = saved;

        while (*line == '\r' || *line == '\n') line++;
    }

    free(content);
    return true;
}

bool config_create_default_file(const char *path) {
    char target[MAX_PATH_LEN];
    if (path && path[0] != '\0') {
        fs_expand_path(path, target, sizeof(target));
    } else {
        fs_expand_path("~/.config/flint/config", target, sizeof(target));
    }

    if (fs_file_exists(target)) {
        return false; /* Already exists: never overwrite an existing config. */
    }

    FILE *f = fopen(target, "w");
    if (!f) return false;

    fprintf(f, "# ==========================================================\n");
    fprintf(f, "# Flint Configuration File\n");
    fprintf(f, "# ==========================================================\n\n");
    fprintf(f, "# Preferred terminal client: iterm2, ghostty, kitty, apple-terminal,\n");
    fprintf(f, "# gnome-terminal, alacritty, konsole, windows-terminal, powershell\n");
    fprintf(f, "# Leave blank to auto-detect the current terminal.\n");
    fprintf(f, "terminal = \n\n");
    fprintf(f, "# Which sessions to resume: user (default), all, or agent\n");
    fprintf(f, "resume_mode = user\n\n");
    fprintf(f, "# Markdown work-log / journal repository path\n");
    fprintf(f, "journal_repo = ~/notes/flint-journal\n\n");
    fprintf(f, "# Automatically record morning session log to journal\n");
    fprintf(f, "auto_journal = false\n\n");
    fprintf(f, "# Open sessions in terminal tabs instead of new windows\n");
    fprintf(f, "open_in_tabs = true\n\n");
    fprintf(f, "# Lookback window in hours (default 24)\n");
    fprintf(f, "lookback_hours = 24\n\n");
    fprintf(f, "# Query connected MCP tools (Slack, etc.) for today's focus\n");
    fprintf(f, "mcp_focus = true\n\n");
    fprintf(f, "# ----------------------------------------------------------\n");
    fprintf(f, "# Tool configurations\n");
    fprintf(f, "# ----------------------------------------------------------\n");
    fprintf(f, "# Each [tool] section configures one CLI the adapters discover (e.g.\n");
    fprintf(f, "# [agy], [claude], [codex]). `wrapper` prepends a launcher binary to the\n");
    fprintf(f, "# resume command, so GrapeRoot-like tools look like:\n");
    fprintf(f, "#\n");
    fprintf(f, "#   [agy]\n");
    fprintf(f, "#   wrapper = graperoot\n");
    fprintf(f, "#   model = gemini-3.8-flash\n");
    fprintf(f, "#   effort = high\n");
    fprintf(f, "#\n");
    fprintf(f, "# Leave wrapper blank to run the CLI without a launcher.\n\n");
    fprintf(f, "[agy]\n");
    fprintf(f, "wrapper = \n");
    fprintf(f, "model = gemini-3.8-flash\n");
    fprintf(f, "effort = high\n\n");
    fprintf(f, "[claude]\n");
    fprintf(f, "wrapper = \n");
    fprintf(f, "model = claude-3-7-sonnet\n");
    fprintf(f, "effort = high\n\n");
    fprintf(f, "[codex]\n");
    fprintf(f, "wrapper = \n");
    fprintf(f, "model = o3-mini\n");
    fprintf(f, "effort = high\n\n");
    fprintf(f, "[cline]\n");
    fprintf(f, "wrapper = \n");
    fprintf(f, "model = \n");
    fprintf(f, "effort = \n\n");
    fprintf(f, "[opencode]\n");
    fprintf(f, "wrapper = \n");
    fprintf(f, "model = \n");
    fprintf(f, "effort = \n");

    fclose(f);
    return true;
}
