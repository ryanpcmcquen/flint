#include "mcp.h"
#include "../config/config.h"
#include "../util/exec.h"
#include "../util/fs.h"
#include "../util/json.h"
#include <string.h>

#ifdef _WIN32
  #include <direct.h>
  #define getcwd _getcwd
#else
  #include <unistd.h>
#endif

static bool csv_contains(const char *csv, const char *name) {
    if (!csv || !name || name[0] == '\0') return false;

    size_t name_len = strlen(name);
    const char *p = csv;
    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        const char *start = p;
        while (*p && *p != ',') p++;

        size_t len = (size_t)(p - start);
        while (len > 0 && start[len - 1] == ' ') len--;
        if (len == name_len && strncmp(start, name, name_len) == 0) {
            return true;
        }
    }
    return false;
}

static void append_server(char *out, size_t out_size, const char *name) {
    if (!out || out_size == 0 || !name || name[0] == '\0') return;
    if (csv_contains(out, name)) return;

    size_t used = strlen(out);
    if (used > 0 && used + 2 < out_size) {
        out[used++] = ',';
        out[used++] = ' ';
        out[used] = '\0';
    }

    size_t name_len = strlen(name);
    if (used + name_len >= out_size) {
        name_len = out_size - used - 1;
    }
    memcpy(out + used, name, name_len);
    out[used + name_len] = '\0';
}

bool mcp_parse_connected_servers(const char *json, char *out, size_t out_size) {
    if (!json || !out || out_size == 0) return false;
    out[0] = '\0';

    size_t json_len = strlen(json);
    char *data = (char *)malloc(json_len + 1);
    const char *scan = json;

    if (data) {
        data[0] = '\0';
        if (json_get_block(json, "data", data, json_len + 1)) {
            scan = data;
        }
    }

    const char *p = scan;
    while (true) {
        const char *end = NULL;
        const char *object = json_next_object(p, &end);
        if (!object) break;

        size_t len = (size_t)(end - object);
        char *slice = (char *)malloc(len + 1);
        if (!slice) break;

        memcpy(slice, object, len);
        slice[len] = '\0';

        char name[128] = {0};
        if (json_get_string(slice, "name", name, sizeof(name))) {
            bool connected = false;

            char status_block[1024] = {0};
            if (json_get_block(slice, "status", status_block, sizeof(status_block))) {
                char status[64] = {0};
                if (json_get_string(status_block, "status", status, sizeof(status))) {
                    connected = strcmp(status, "connected") == 0;
                }
            }

            if (!connected) {
                char status[64] = {0};
                if (json_get_string(slice, "status", status, sizeof(status))) {
                    connected = strcmp(status, "connected") == 0;
                }
            }

            if (connected) {
                append_server(out, out_size, name);
            }
        }

        free(slice);
        p = end;
    }

    free(data);
    return out[0] != '\0';
}

bool mcp_detect_connected(const session_list_t *list, mcp_target_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!exec_command_exists("opencode")) return false;

    char workspaces[8][MAX_PATH_LEN];
    size_t workspace_count = 0;

    if (list) {
        for (size_t i = 0; i < list->count && workspace_count < 8; i++) {
            const char *ws = list->items[i].workspace;
            if (ws[0] == '\0') continue;

            bool seen = false;
            for (size_t j = 0; j < workspace_count; j++) {
                if (strcmp(workspaces[j], ws) == 0) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                strncpy(workspaces[workspace_count], ws, MAX_PATH_LEN - 1);
                workspaces[workspace_count][MAX_PATH_LEN - 1] = '\0';
                workspace_count++;
            }
        }
    }

    if (workspace_count == 0) {
        char cwd[MAX_PATH_LEN] = {0};
        if (!getcwd(cwd, sizeof(cwd))) {
            strncpy(cwd, ".", sizeof(cwd) - 1);
        }
        strncpy(workspaces[0], cwd, MAX_PATH_LEN - 1);
        workspace_count = 1;
    }

    for (size_t i = 0; i < workspace_count; i++) {
        size_t param_size = strlen(workspaces[i]) + 32;
        char *param = (char *)malloc(param_size);
        if (!param) continue;
        snprintf(param, param_size, "location[directory]=%s", workspaces[i]);

        size_t quoted_size = strlen(param) * 4 + 8;
        char *quoted_param = (char *)malloc(quoted_size);
        if (!quoted_param) {
            free(param);
            continue;
        }
        exec_shell_quote(param, quoted_param, quoted_size);
        free(param);

        size_t cmd_size = strlen(quoted_param) + 64;
        char *cmd = (char *)malloc(cmd_size);
        if (!cmd) {
            free(quoted_param);
            continue;
        }
        snprintf(cmd, cmd_size, "opencode api mcp.list --param %s", quoted_param);
        free(quoted_param);

        char *json = exec_capture(cmd);
        free(cmd);
        if (!json) continue;

        char servers[512] = {0};
        bool found = mcp_parse_connected_servers(json, servers, sizeof(servers));
        free(json);

        if (found) {
            strncpy(out->servers, servers, sizeof(out->servers) - 1);
            strncpy(out->workspace, workspaces[i], sizeof(out->workspace) - 1);
            if (out->servers[0] != '\0') {
                strncpy(out->server_name, out->servers, sizeof(out->server_name) - 1);
                out->server_name[sizeof(out->server_name) - 1] = '\0';
                char *comma = strchr(out->server_name, ',');
                if (comma) *comma = '\0';
            }
            return true;
        }
    }

    return false;
}

static int mcp_collect_line(const char *line, size_t len, size_t line_num, void *userdata) {
    (void)line_num;
    mcp_run_t *ctx = (mcp_run_t *)userdata;

    char *buf = (char *)malloc(len + 1);
    if (!buf) return 0;

    memcpy(buf, line, len);
    buf[len] = '\0';

    char session_id[MAX_NAME_LEN] = {0};
    if (ctx->session_id[0] == '\0' &&
        json_get_string(buf, "sessionID", session_id, sizeof(session_id))) {
        strncpy(ctx->session_id, session_id, sizeof(ctx->session_id) - 1);
    }

    char type[32] = {0};
    if (json_get_string(buf, "type", type, sizeof(type)) &&
        strcmp(type, "text") == 0) {

        char *part = (char *)malloc(len + 1);
        if (part) {
            part[0] = '\0';
            if (json_get_block(buf, "part", part, len + 1)) {
                char *text = (char *)malloc(len + 1);
                if (text) {
                    text[0] = '\0';
                    if (json_get_string(part, "text", text, len + 1)) {
                        size_t text_len = strlen(text);
                        if (ctx->len + text_len + 2 < sizeof(ctx->text)) {
                            if (ctx->len > 0) {
                                ctx->text[ctx->len++] = '\n';
                            }
                            memcpy(ctx->text + ctx->len, text, text_len);
                            ctx->len += text_len;
                            ctx->text[ctx->len] = '\0';
                        }
                    }
                    free(text);
                }
            }
            free(part);
        }
    }

    free(buf);
    return 0;
}

#ifdef TEST_MODE
int flint_mcp_collect_line(const char *line, size_t len, void *ctx) {
    return mcp_collect_line(line, len, 0, ctx);
}
#endif

static void mcp_remove_session(const char *session_id) {
    if (!session_id || session_id[0] == '\0') return;

    char param[MAX_NAME_LEN + 32];
    snprintf(param, sizeof(param), "sessionID=%s", session_id);

    char quoted_param[MAX_NAME_LEN * 2 + 64];
    exec_shell_quote(param, quoted_param, sizeof(quoted_param));

    char cmd[MAX_NAME_LEN * 3 + 128];
    snprintf(cmd, sizeof(cmd), "opencode api session.remove --param %s", quoted_param);

    char *ignored = exec_capture(cmd);
    free(ignored);
}

static const char *const MCP_FOCUS_INSTRUCTIONS =
    "You are preparing a short morning focus briefing. You may use only read-only tools and queries.\n\n"
    "Recent work context from the user's last working day:\n%s\n"
    "Connected tools available: %s.\n\n"
    "Use read-only searches in those tools (for example messaging platforms such as Slack or "
    "Microsoft Teams, recent issues, or documents) to find what genuinely needs attention today.\n"
    "Reply with only 3 to 5 terse bullet points, each starting with \"- \", describing concrete "
    "priorities for today. Do not write, send, or change anything. Do not explain your process.";

static bool run_prompt(const char *workspace, const char *context, const char *servers,
                      const char *instructions, char *out, size_t out_size, char *session_id_out) {
    if (!workspace || !context || !instructions || !out || out_size == 0) return false;
    out[0] = '\0';

    size_t prompt_size = strlen(context) + strlen(instructions) + 512;
    char *prompt = (char *)malloc(prompt_size);
    if (!prompt) return false;

    snprintf(prompt, prompt_size, instructions, context, servers ? servers : "none");

    size_t quoted_prompt_size = strlen(prompt) * 4 + 8;
    size_t quoted_workspace_size = strlen(workspace) * 4 + 8;
    char *quoted_prompt = (char *)malloc(quoted_prompt_size);
    char *quoted_workspace = (char *)malloc(quoted_workspace_size);
    char *quoted_title = (char *)malloc(64);

    if (!quoted_prompt || !quoted_workspace || !quoted_title) {
        free(prompt);
        free(quoted_prompt);
        free(quoted_workspace);
        free(quoted_title);
        return false;
    }

    exec_shell_quote(prompt, quoted_prompt, quoted_prompt_size);
    exec_shell_quote(workspace, quoted_workspace, quoted_workspace_size);
    exec_shell_quote("Flint focus query", quoted_title, 64);

    size_t cmd_size = strlen(quoted_prompt) + strlen(quoted_workspace) +
                      strlen(quoted_title) + 256;
    char *cmd = (char *)malloc(cmd_size);
    if (!cmd) {
        free(prompt);
        free(quoted_prompt);
        free(quoted_workspace);
        free(quoted_title);
        return false;
    }

    snprintf(cmd, cmd_size,
             "cd %s && opencode run --agent plan --auto --format json --title %s %s",
             quoted_workspace, quoted_title, quoted_prompt);

    char *output = exec_capture(cmd);
    free(prompt);
    free(quoted_prompt);
    free(quoted_workspace);
    free(quoted_title);
    free(cmd);

    if (!output) return false;

    mcp_run_t run;
    memset(&run, 0, sizeof(run));
    jsonl_for_each_line(output, mcp_collect_line, &run);
    free(output);

    mcp_remove_session(run.session_id);

    if (run.text[0] == '\0') return false;

    str_trim(run.text);
    strncpy(out, run.text, out_size - 1);
    out[out_size - 1] = '\0';

    if (session_id_out) {
        strncpy(session_id_out, run.session_id, MAX_NAME_LEN - 1);
        session_id_out[MAX_NAME_LEN - 1] = '\0';
    }

    return true;
}

bool mcp_query_focus(const flint_config_t *cfg, const mcp_target_t *target,
                     const char *focus_context, char *out, size_t out_size) {
    if (!cfg || !cfg->mcp_focus || !target || !out || out_size == 0) return false;
    out[0] = '\0';

    if (target->servers[0] == '\0' || target->workspace[0] == '\0') return false;

    const char *context = (focus_context && focus_context[0] != '\0')
                        ? focus_context
                        : "- No local session context available.\n";

    return run_prompt(target->workspace, context, target->servers, MCP_FOCUS_INSTRUCTIONS, out, out_size, NULL);
}

bool mcp_query_prompt(const char *workspace, const char *context, const char *servers,
                     const char *instructions, char *out, size_t out_size, char *session_id_out) {
    if (!workspace || !context || !instructions || !out || out_size == 0) return false;
    return run_prompt(workspace, context, servers, instructions, out, out_size, session_id_out);
}
