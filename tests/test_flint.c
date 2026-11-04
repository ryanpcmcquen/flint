#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "../source/flint.h"
#include "../source/ai/mcp.h"
#include "../source/ai/ai_adapter.h"
#include "../source/brief/brief.h"
#include "../source/config/config.h"
#include "../source/git/git.h"
#include "../source/util/fs.h"
#include "../source/util/json.h"
#include "../source/terminal/terminal.h"
#include "../source/journal/journal.h"
#include "../source/process/proc.h"

static int total_tests = 0;
static int passed_tests = 0;

#define TEST(name) \
    do { \
        total_tests++; \
        printf("  Testing %-35s ... ", name); \
    } while(0)

#define PASS() \
    do { \
        passed_tests++; \
        printf(ANSI_GREEN "PASSED" ANSI_RESET "\n"); \
    } while(0)

static void test_json_primitives(void) {
    TEST("json_get_string");
    const char *json = "{\"tool\": \"antigravity\", \"version\": 2, \"active\": true}";
    char tool[64] = {0};
    assert(json_get_string(json, "tool", tool, sizeof(tool)));
    assert(strcmp(tool, "antigravity") == 0);
    PASS();

    TEST("json_get_int");
    long version = 0;
    assert(json_get_int(json, "version", &version));
    assert(version == 2);
    PASS();

    TEST("json_get_bool");
    bool active = false;
    assert(json_get_bool(json, "active", &active));
    assert(active == true);
    PASS();

    TEST("json_get_long_long");
    const char *big_json = "{\"updated\": 1793641153470}";
    long long updated = 0;
    assert(json_get_long_long(big_json, "updated", &updated));
    assert(updated == 1793641153470LL);
    PASS();

    TEST("json_next_object");
    const char *objects = "[{\"name\": \"one\"}, {\"name\": \"two\"}]";
    const char *end = NULL;
    const char *first = json_next_object(objects, &end);
    assert(first != NULL);
    assert(end != NULL);
    assert(strncmp(first, "{\"name\": \"one\"}", (size_t)(end - first)) == 0);
    const char *second = json_next_object(end, &end);
    assert(second != NULL);
    assert(strncmp(second, "{\"name\": \"two\"}", (size_t)(end - second)) == 0);
    PASS();
}

static void test_json_unescape(void) {
    TEST("json_unescape");
    const char *escaped = "Hello\\nWorld\\t\\\"Test\\\"\\\\";
    char unescaped[64] = {0};
    json_unescape(escaped, unescaped, sizeof(unescaped));
    assert(strcmp(unescaped, "Hello\nWorld\t\"Test\"\\") == 0);
    PASS();
}

static int count_lines_cb(const char *line, size_t len, size_t line_num, void *arg) {
    (void)line;
    (void)len;
    (void)line_num;
    int *c = (int *)arg;
    (*c)++;
    return 0;
}

static void test_jsonl_scanning(void) {
    TEST("jsonl_for_each_line");
    const char *jsonl = "{\"id\": 1}\n{\"id\": 2}\r\n{\"id\": 3}\n";
    int count = 0;
    jsonl_for_each_line(jsonl, count_lines_cb, &count);
    assert(count == 3);
    PASS();
}

static void write_test_file(const char *path, const char *content);

static void test_path_utils(void) {
    TEST("fs_path_join");
    char out[MAX_PATH_LEN];
    fs_path_join("/path/to", "project", out, sizeof(out));
#ifdef _WIN32
    assert(strcmp(out, "/path/to\\project") == 0 || strcmp(out, "\\path\\to\\project") == 0);
#else
    assert(strcmp(out, "/path/to/project") == 0);
#endif
    PASS();

    TEST("str_has_prefix / suffix");
    assert(str_has_prefix("flint-tool", "flint"));
    assert(!str_has_prefix("tool", "flint"));
    assert(str_has_suffix("flint.exe", ".exe"));
    assert(!str_has_suffix("flint.exe", ".zip"));
    PASS();

    TEST("str_contains_ignore_case");
    assert(str_contains_ignore_case("Ghostty Terminal", "ghostty"));
    assert(str_contains_ignore_case("iTerm2", "ITERM"));
    assert(!str_contains_ignore_case("kitty", "alacritty"));
    PASS();

    TEST("str_collapse_whitespace");
    char spaced[64];
    strncpy(spaced, "  Fix the\n\tparser   please  ", sizeof(spaced));
    str_collapse_whitespace(spaced);
    assert(strcmp(spaced, "Fix the parser please") == 0);
    PASS();

    TEST("fs_read_head reads a bounded prefix");
    char head_path[MAX_PATH_LEN];
    snprintf(head_path, sizeof(head_path), "/tmp/flint_test_head_%d", (int)getpid());
    write_test_file(head_path, "0123456789");
    size_t head_size = 0;
    char *head = fs_read_head(head_path, 4, &head_size);
    assert(head != NULL);
    assert(head_size == 4);
    assert(strcmp(head, "0123") == 0);
    free(head);
    remove(head_path);
    PASS();
}

static void test_session_deduplication(void) {
    TEST("session_list deduplication");
    session_list_t list;
    session_list_init(&list);

    session_t s1 = {
        .last_active = 100,
        .workspace = "test-project",
        .tool_cmd = "agy",
        .origin = SESSION_ORIGIN_USER
    };
    strncpy(s1.summary, "Early commit", sizeof(s1.summary));
    session_list_add(&list, &s1);

    session_t s2 = {
        .last_active = 200,
        .workspace = "test-project",
        .tool_cmd = "agy",
        .origin = SESSION_ORIGIN_USER
    };
    strncpy(s2.summary, "Later commit", sizeof(s2.summary));
    session_list_add(&list, &s2);

    assert(list.count == 1);
    assert(list.items[0].last_active == 200);
    assert(strcmp(list.items[0].summary, "Later commit") == 0);

    session_list_free(&list);
    PASS();
}

static void test_terminal_registry(void) {
    TEST("terminal_find");
#if defined(__APPLE__)
    const terminal_adapter_t *t1 = terminal_find("apple-terminal");
    assert(t1 != NULL);
    const terminal_adapter_t *t2 = terminal_find("iterm2");
    assert(t2 != NULL);
    const terminal_adapter_t *t3 = terminal_find("ghostty");
    assert(t3 != NULL);
    const terminal_adapter_t *t4 = terminal_find("kitty");
    assert(t4 != NULL);
#elif defined(_WIN32)
    const terminal_adapter_t *t1 = terminal_find("windows-terminal");
    assert(t1 != NULL);
    const terminal_adapter_t *t2 = terminal_find("powershell");
    assert(t2 != NULL);
#else
    const terminal_adapter_t *t1 = terminal_find("gnome-terminal");
    assert(t1 != NULL);
    const terminal_adapter_t *t2 = terminal_find("kitty");
    assert(t2 != NULL);
#endif
    PASS();
}

static void test_config_and_wrappers(void) {
    TEST("config_init_defaults (no default wrapper)");
    flint_config_t cfg;
    config_init_defaults(&cfg);
    assert(cfg.tool_config_count == 6);
    assert(cfg.mcp_focus == true);
    const tool_config_t *agy = config_find_tool(&cfg, "agy");
    assert(agy != NULL);
    assert(agy->wrapper[0] == '\0');
    assert(strcmp(agy->model, "gemini-3.8-flash") == 0);
    assert(strcmp(agy->effort, "high") == 0);
    config_destroy(&cfg);
    PASS();

    TEST("config_find_tool lookup");
    flint_config_t cfg2;
    config_init_defaults(&cfg2);
    assert(config_find_tool(&cfg2, "agy") != NULL);
    assert(config_find_tool(&cfg2, "claude") != NULL);
    assert(config_find_tool(&cfg2, "codex") != NULL);
    assert(config_find_tool(&cfg2, "cline") != NULL);
    assert(config_find_tool(&cfg2, "opencode") != NULL);
    assert(config_find_tool(&cfg2, "grafareoot") == NULL);
    config_destroy(&cfg2);
    PASS();

    TEST("config_format_tool_cmd without wrapper");
    flint_config_t cfg3;
    config_init_defaults(&cfg3);
    char cmd[512] = {0};
    config_format_tool_cmd(&cfg3, "agy", "session-123", cmd, sizeof(cmd));
    assert(strstr(cmd, "agy") != NULL);
    assert(strstr(cmd, "--model gemini-3.8-flash") != NULL);
    assert(strstr(cmd, "--effort high") != NULL);
    config_destroy(&cfg3);
    PASS();

    TEST("config_format_tool_cmd with GrapeRoot-like wrapper");
    flint_config_t cfg4;
    config_init_defaults(&cfg4);
    config_add_tool(&cfg4, "custom", "graperoot", "", "");
    char cmd2[512] = {0};
    config_format_tool_cmd(&cfg4, "custom", "session-999", cmd2, sizeof(cmd2));
    assert(strstr(cmd2, "graperoot custom") != NULL);
    config_destroy(&cfg4);
    PASS();

    TEST("config_format_tool_cmd resumes Cline sessions");
    flint_config_t cfg5;
    config_init_defaults(&cfg5);
    char cmd3[512] = {0};
    config_format_tool_cmd(&cfg5, "cline", "session-555", cmd3, sizeof(cmd3));
    assert(strstr(cmd3, "cline --id session-555") != NULL);
    config_destroy(&cfg5);
    PASS();

    TEST("config_format_tool_cmd resumes OpenCode sessions");
    flint_config_t cfg6;
    config_init_defaults(&cfg6);
    char cmd4[512] = {0};
    config_format_tool_cmd(&cfg6, "opencode", "ses_abc123", cmd4, sizeof(cmd4));
    assert(strstr(cmd4, "opencode --session ses_abc123") != NULL);
    config_destroy(&cfg6);
    PASS();

    TEST("config_format_tool_cmd resumes Antigravity sessions via --conversation");
    flint_config_t cfg7;
    config_init_defaults(&cfg7);
    char cmd5[512] = {0};
    config_format_tool_cmd(&cfg7, "agy", "conv-abc123", cmd5, sizeof(cmd5));
    assert(strstr(cmd5, "agy --conversation conv-abc123") != NULL);
    assert(strstr(cmd5, "--model gemini-3.8-flash") != NULL);
    assert(strstr(cmd5, "--effort high") != NULL);
    char cmd6[512] = {0};
    config_format_tool_cmd(&cfg7, "agy", "", cmd6, sizeof(cmd6));
    assert(strstr(cmd6, "--conversation") == NULL);
    config_destroy(&cfg7);
    PASS();

    TEST("config_format_tool_cmd resumes Claude sessions via --resume");
    flint_config_t cfg8;
    config_init_defaults(&cfg8);
    char cmd7[512] = {0};
    config_format_tool_cmd(&cfg8, "claude", "claude-session-123", cmd7, sizeof(cmd7));
    assert(strstr(cmd7, "claude --resume claude-session-123") != NULL);
    assert(strstr(cmd7, "--model claude-3-7-sonnet") != NULL);
    config_destroy(&cfg8);
    PASS();

    TEST("config_format_tool_cmd resumes Codex sessions via resume subcommand");
    flint_config_t cfg9;
    config_init_defaults(&cfg9);
    char cmd8[512] = {0};
    config_format_tool_cmd(&cfg9, "codex", "codex-session-456", cmd8, sizeof(cmd8));
    assert(strstr(cmd8, "codex resume codex-session-456") != NULL);
    assert(strstr(cmd8, "--model o3-mini") != NULL);
    config_destroy(&cfg9);
    PASS();

    TEST("config_format_tool_cmd resumes OpenClaw sessions via resume");
    flint_config_t cfg10;
    config_init_defaults(&cfg10);
    char cmd9[512] = {0};
    config_format_tool_cmd(&cfg10, "openclaw", "agent:main:main", cmd9, sizeof(cmd9));
    assert(strstr(cmd9, "openclaw resume agent:main:main") != NULL);
    config_destroy(&cfg10);
    PASS();

    TEST("config defaults include OpenClaw and open_in_tabs");
    flint_config_t cfg11;
    config_init_defaults(&cfg11);
    assert(cfg11.open_in_tabs == true);
    assert(config_find_tool(&cfg11, "openclaw") != NULL);
    config_destroy(&cfg11);
    PASS();

    TEST("config_set_wrapper_all applies the --wrapper override");
    flint_config_t cfg12;
    config_init_defaults(&cfg12);
    config_set_wrapper_all(&cfg12, "graperoot");
    for (size_t i = 0; i < cfg12.tool_config_count; i++) {
        assert(strcmp(cfg12.tool_configs[i].wrapper, "graperoot") == 0);
    }
    char cmd10[512] = {0};
    config_format_tool_cmd(&cfg12, "openclaw", "agent:main:main", cmd10, sizeof(cmd10));
    assert(strncmp(cmd10, "graperoot openclaw resume", 25) == 0);

    /* An empty override clears the launcher. */
    config_set_wrapper_all(&cfg12, "");
    char cmd11[512] = {0};
    config_format_tool_cmd(&cfg12, "openclaw", "agent:main:main", cmd11, sizeof(cmd11));
    assert(strncmp(cmd11, "openclaw resume", 15) == 0);
    config_destroy(&cfg12);
    PASS();
}

static void test_config_file_parsing(void) {
    TEST("config file parsing: [agy] wrapper, model, effort");
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "/tmp/flint_test_config_%d", (int)getpid());
    FILE *f = fopen(path, "w");
    assert(f);
    fprintf(f, "[agy]\n");
    fprintf(f, "wrapper = graperoot\n");
    fprintf(f, "model = gemini-3.1-flash\n");
    fprintf(f, "effort = low\n");
    fclose(f);

    flint_config_t cfg;
    config_init_defaults(&cfg);
    assert(config_load(path, &cfg));
    const tool_config_t *tc = config_find_tool(&cfg, "agy");
    assert(tc != NULL);
    assert(strcmp(tc->wrapper, "graperoot") == 0);
    assert(strcmp(tc->model, "gemini-3.1-flash") == 0);
    assert(strcmp(tc->effort, "low") == 0);
    config_destroy(&cfg);
    remove(path);
    PASS();

    TEST("config file parsing: [codex] alias section");
    snprintf(path, sizeof(path), "/tmp/flint_test_config_%d", (int)getpid());
    f = fopen(path, "w");
    assert(f);
    fprintf(f, "[codex]\n");
    fprintf(f, "wrapper = wrapcodex\n");
    fprintf(f, "model = o3-100\n");
    fprintf(f, "effort = medium\n");
    fclose(f);

    flint_config_t cfg2;
    config_init_defaults(&cfg2);
    assert(config_load(path, &cfg2));
    tc = config_find_tool(&cfg2, "codex");
    assert(tc != NULL);
    assert(strcmp(tc->wrapper, "wrapcodex") == 0);
    assert(strcmp(tc->model, "o3-100") == 0);
    assert(strcmp(tc->effort, "medium") == 0);
    config_destroy(&cfg2);
    remove(path);
    PASS();

    TEST("config file parsing: [openclaw] section");
    snprintf(path, sizeof(path), "/tmp/flint_test_config_%d", (int)getpid());
    f = fopen(path, "w");
    assert(f);
    fprintf(f, "[openclaw]\n");
    fprintf(f, "wrapper = wrapclaw\n");
    fprintf(f, "model = claw-model\n");
    fprintf(f, "effort = high\n");
    fclose(f);

    flint_config_t cfg5;
    config_init_defaults(&cfg5);
    assert(config_load(path, &cfg5));
    tc = config_find_tool(&cfg5, "openclaw");
    assert(tc != NULL);
    assert(strcmp(tc->wrapper, "wrapclaw") == 0);
    assert(strcmp(tc->model, "claw-model") == 0);
    assert(strcmp(tc->effort, "high") == 0);
    config_destroy(&cfg5);
    remove(path);
    PASS();

    TEST("config file parsing: mcp_focus");
    snprintf(path, sizeof(path), "/tmp/flint_test_config_%d", (int)getpid());
    f = fopen(path, "w");
    assert(f);
    fprintf(f, "mcp_focus = false\n");
    fclose(f);

    flint_config_t cfg3;
    config_init_defaults(&cfg3);
    assert(config_load(path, &cfg3));
    assert(cfg3.mcp_focus == false);
    config_destroy(&cfg3);
    remove(path);
    PASS();

    TEST("config file parsing: open_in_tabs");
    snprintf(path, sizeof(path), "/tmp/flint_test_config_%d", (int)getpid());
    f = fopen(path, "w");
    assert(f);
    fprintf(f, "open_in_tabs = false\n");
    fclose(f);

    flint_config_t cfg4;
    config_init_defaults(&cfg4);
    assert(config_load(path, &cfg4));
    assert(cfg4.open_in_tabs == false);
    config_destroy(&cfg4);
    remove(path);
    PASS();
}

static void test_brief_and_mcp(void) {
    TEST("brief_build ranks unfinished work first");
    session_list_t list;
    session_list_init(&list);

    session_t older = {0};
    older.last_active = 100;
    strncpy(older.workspace, "/tmp/alpha", sizeof(older.workspace) - 1);
    strncpy(older.tool_name, "Cline", sizeof(older.tool_name) - 1);
    strncpy(older.summary, "Older alpha work", sizeof(older.summary) - 1);

    session_t newer = {0};
    newer.last_active = 200;
    strncpy(newer.workspace, "/tmp/alpha", sizeof(newer.workspace) - 1);
    strncpy(newer.tool_name, "OpenCode", sizeof(newer.tool_name) - 1);
    strncpy(newer.summary, "Newer alpha work", sizeof(newer.summary) - 1);
    newer.git.is_git_repo = true;
    newer.git.uncommitted_count = 2;
    strncpy(newer.git.suggested_focus, "Finish uncommitted changes in source.c",
            sizeof(newer.git.suggested_focus) - 1);

    session_t beta = {0};
    beta.last_active = 150;
    strncpy(beta.workspace, "/Users/test/beta", sizeof(beta.workspace) - 1);
    strncpy(beta.tool_name, "Codex", sizeof(beta.tool_name) - 1);
    strncpy(beta.summary, "Refactor the parser", sizeof(beta.summary) - 1);

    session_list_add(&list, &older);
    session_list_add(&list, &newer);
    session_list_add(&list, &beta);

    brief_t brief;
    brief_build(&list, &brief);
    assert(brief.focus_count == 2);
    assert(strcmp(brief.focus[0].workspace, "/tmp/alpha") == 0);
    assert(strstr(brief.focus[0].text, "uncommitted") != NULL);
    assert(strcmp(brief.focus[1].workspace, "/Users/test/beta") == 0);
    assert(strstr(brief.focus[1].text, "Refactor the parser") != NULL);

    session_list_free(&list);
    PASS();

    TEST("mcp_parse_connected_servers");
    const char *mcp_json =
        "{\"location\":{\"directory\":\"/tmp\"},\"data\":["
        "{\"name\":\"slack\",\"status\":{\"status\":\"connected\"}},"
        "{\"name\":\"linear\",\"status\":{\"status\":\"disabled\"}},"
        "{\"name\":\"notion\",\"status\":{\"status\":\"connected\"}}]}";
    char servers[256] = {0};
    assert(mcp_parse_connected_servers(mcp_json, servers, sizeof(servers)));
    assert(strcmp(servers, "slack, notion") == 0);

    const char *no_mcp = "{\"data\":[{\"name\":\"slack\",\"status\":{\"status\":\"disabled\"}}]}";
    assert(!mcp_parse_connected_servers(no_mcp, servers, sizeof(servers)));
    PASS();

    TEST("brief_add_mcp_focus adds bullets");
    brief_t brief2;
    memset(&brief2, 0, sizeof(brief2));
    bool added = brief_add_mcp_focus(&brief2, "- Review the Slack thread\n- Finish the README changes\n", 5, "slack");
    assert(added);
    assert(brief2.focus_count == 2);
    assert(strcmp(brief2.focus[0].text, "Review the Slack thread") == 0);
    assert(strcmp(brief2.focus[0].workspace, "slack") == 0);
    assert(strcmp(brief2.focus[1].text, "Finish the README changes") == 0);
    PASS();
}


static void test_antigravity_transcript_parsing(void) {
    TEST("antigravity transcript line parse (regression: no invalid free)");
    agy_scan_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.origin = SESSION_ORIGIN_USER;

    const char *line = "{\"step_index\":0,\"source\":\"USER_EXPLICIT\",\"type\":\"USER_INPUT\",\"status\":\"DONE\",\"created_at\":\"2026-11-03T21:41:23Z\",\"content\":\"<USER_REQUEST>\\nDo I have any usage left?\\n</USER_REQUEST>\\n\"}";
    int rc = flint_agy_parse_line(line, (int)strlen(line), &ctx);
    assert(rc == 0);
    assert(ctx.origin == SESSION_ORIGIN_USER);
    assert(ctx.prompt_count == 1);
    assert(strstr(ctx.last_user_req, "usage left") != NULL);
    PASS();

    TEST("antigravity transcript line parse reuses scan ctx scratch buffer");
    ctx.prompt_count = 0;
    ctx.last_user_req[0] = '\0';
    rc = flint_agy_parse_line(line, (int)strlen(line), &ctx);
    assert(rc == 0);
    assert(ctx.prompt_count == 1);
    assert(strstr(ctx.last_user_req, "usage left") != NULL);
    PASS();

    TEST("antigravity transcript line parse skips overlong lines");
    char long_line[1300];
    memset(long_line, 'a', 1200);
    long_line[1200] = '\0';
    agy_scan_ctx_t ctx2;
    memset(&ctx2, 0, sizeof(ctx2));
    rc = flint_agy_parse_line(long_line, 1200, &ctx2);
    assert(rc == 0);
    assert(ctx2.prompt_count == 0);
    PASS();
}

static void test_ai_head_parsers(void) {
    TEST("claude head parse: session, cwd, first prompt");
    const char *claude_jsonl =
        "{\"type\":\"summary\",\"summary\":\"Generated fallback title\"}\n"
        "{\"parentUuid\":null,\"isSidechain\":false,\"cwd\":\"/tmp/claude-work\","
        "\"sessionId\":\"11111111-1111-4111-8111-111111111111\",\"type\":\"user\","
        "\"message\":{\"role\":\"user\",\"content\":[{\"type\":\"text\","
        "\"text\":\"Fix the JSON parser\\nplease\"}]},"
        "\"timestamp\":\"2026-11-04T12:00:00Z\"}\n";
    claude_head_t claude;
    assert(flint_claude_parse_head(claude_jsonl, &claude));
    assert(strcmp(claude.session_id, "11111111-1111-4111-8111-111111111111") == 0);
    assert(strcmp(claude.workspace, "/tmp/claude-work") == 0);
    assert(strcmp(claude.summary, "Fix the JSON parser please") == 0);
    assert(claude.is_sidechain == false);
    PASS();

    TEST("claude head parse: custom title and sidechain");
    const char *claude_title =
        "{\"isSidechain\":true,\"cwd\":\"/tmp/agent-work\","
        "\"sessionId\":\"33333333-3333-4333-8333-333333333333\",\"type\":\"user\","
        "\"message\":{\"role\":\"user\",\"content\":\"Subagent prompt\"}}\n"
        "{\"type\":\"custom-title\",\"customTitle\":\"Named Claude Session\"}\n";
    claude_head_t titled;
    assert(flint_claude_parse_head(claude_title, &titled));
    assert(titled.is_sidechain == true);
    assert(strcmp(titled.summary, "Named Claude Session") == 0);
    PASS();

    TEST("claude head parse: malformed input is rejected");
    claude_head_t bad_claude;
    assert(flint_claude_parse_head("not json at all", &bad_claude) == false);
    PASS();

    TEST("codex head parse: session_meta + user_message");
    const char *codex_jsonl =
        "{\"timestamp\":\"2026-11-04T12:00:00Z\",\"type\":\"session_meta\","
        "\"payload\":{\"id\":\"22222222-2222-4222-8222-222222222222\","
        "\"session_id\":\"22222222-2222-4222-8222-222222222222\","
        "\"timestamp\":\"2026-11-04T12:00:00Z\",\"cwd\":\"/tmp/codex-work\","
        "\"originator\":\"codex_cli_rs\",\"cli_version\":\"1.0\","
        "\"git\":{\"branch\":\"main\"}}}\n"
        "{\"timestamp\":\"2026-11-04T12:00:01Z\",\"type\":\"event_msg\","
        "\"payload\":{\"type\":\"user_message\",\"message\":\"Add real Codex discovery\","
        "\"images\":[]}}\n";
    codex_head_t codex;
    assert(flint_codex_parse_head(codex_jsonl, &codex));
    assert(strcmp(codex.session_id, "22222222-2222-4222-8222-222222222222") == 0);
    assert(strcmp(codex.workspace, "/tmp/codex-work") == 0);
    assert(strcmp(codex.summary, "Add real Codex discovery") == 0);
    assert(strcmp(codex.git_branch, "main") == 0);
    assert(codex.is_agent == false);
    PASS();

    TEST("codex head parse: response_item user message");
    const char *codex_response =
        "{\"type\":\"session_meta\",\"payload\":{\"id\":\"44444444-4444-4444-8444-444444444444\","
        "\"cwd\":\"/tmp/codex-response\"}}\n"
        "{\"type\":\"response_item\",\"payload\":{\"type\":\"message\",\"role\":\"user\","
        "\"content\":[{\"type\":\"input_text\",\"text\":\"New format prompt\"}]}}\n";
    codex_head_t response;
    assert(flint_codex_parse_head(codex_response, &response));
    assert(strcmp(response.summary, "New format prompt") == 0);
    PASS();

    TEST("codex head parse: subagent metadata");
    const char *codex_agent =
        "{\"type\":\"session_meta\",\"payload\":{\"id\":\"55555555-5555-4555-8555-555555555555\","
        "\"cwd\":\"/tmp/codex-agent\",\"parent_thread_id\":\"66666666-6666-4666-8666-666666666666\","
        "\"agent_role\":\"explorer\"}}\n";
    codex_head_t agent;
    assert(flint_codex_parse_head(codex_agent, &agent));
    assert(agent.is_agent == true);
    PASS();

    TEST("codex head parse: malformed input is rejected");
    codex_head_t bad_codex;
    assert(flint_codex_parse_head("{broken", &bad_codex) == false);
    PASS();
}

static void write_test_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(content, f);
    fclose(f);
}

static const ai_adapter_t *find_test_adapter(const char *binary) {
    const ai_adapter_t **adapters = NULL;
    size_t adapter_count = ai_get_adapters(&adapters);
    for (size_t i = 0; i < adapter_count; i++) {
        if (adapters[i]->cli_binary && strcmp(adapters[i]->cli_binary, binary) == 0) {
            return adapters[i];
        }
    }
    return NULL;
}

static void test_claude_codex_discovery(void) {
    TEST("claude discovery from ~/.claude/projects");
    char old_home[MAX_PATH_LEN] = {0};
    char old_codex_home[MAX_PATH_LEN] = {0};
    const char *home_env = getenv("HOME");
    const char *codex_env = getenv("CODEX_HOME");
    if (home_env) strncpy(old_home, home_env, sizeof(old_home) - 1);
    if (codex_env) strncpy(old_codex_home, codex_env, sizeof(old_codex_home) - 1);

    char root[MAX_PATH_LEN];
    char home[MAX_PATH_LEN];
    char claude_projects[MAX_PATH_LEN];
    char claude_project[MAX_PATH_LEN];
    char claude_file[MAX_PATH_LEN];
    char claude_workspace[MAX_PATH_LEN];
    snprintf(root, sizeof(root), "/tmp/flint_ai_test_%d", (int)getpid());
    snprintf(home, sizeof(home), "%s/home", root);
    snprintf(claude_projects, sizeof(claude_projects), "%s/.claude/projects", home);
    snprintf(claude_project, sizeof(claude_project), "%s/-tmp-claude-project", claude_projects);
    snprintf(claude_file, sizeof(claude_file), "%s/11111111-1111-4111-8111-111111111111.jsonl",
             claude_project);
    snprintf(claude_workspace, sizeof(claude_workspace), "%s/claude-workspace", root);

    assert(fs_mkdir_p(claude_project) == 0);
    assert(fs_mkdir_p(claude_workspace) == 0);
    char claude_fixture[2048];
    snprintf(claude_fixture, sizeof(claude_fixture),
             "{\"isSidechain\":false,\"cwd\":\"%s\","
             "\"sessionId\":\"11111111-1111-4111-8111-111111111111\",\"type\":\"user\","
             "\"message\":{\"role\":\"user\",\"content\":\"Fix the JSON parser\"}}\n",
             claude_workspace);
    write_test_file(claude_file, claude_fixture);

    setenv("HOME", home, 1);

    const ai_adapter_t *claude = find_test_adapter("claude");
    assert(claude != NULL);
    flint_config_t cfg;
    config_init_defaults(&cfg);
    session_list_t list;
    session_list_init(&list);
    assert(claude->discover_sessions(&cfg, 24, &list) == 0);
    assert(list.count == 1);
    assert(strcmp(list.items[0].id, "11111111-1111-4111-8111-111111111111") == 0);
    assert(strcmp(list.items[0].workspace, claude_workspace) == 0);
    assert(strcmp(list.items[0].summary, "Fix the JSON parser") == 0);
    assert(strstr(list.items[0].resume_cmd, "claude --resume 11111111-1111-4111-8111-111111111111") != NULL);
    session_list_free(&list);
    config_destroy(&cfg);
    PASS();

    TEST("codex discovery from $CODEX_HOME/sessions");
    char codex_home[MAX_PATH_LEN];
    char codex_day[MAX_PATH_LEN];
    char codex_file[MAX_PATH_LEN];
    char codex_workspace[MAX_PATH_LEN];
    snprintf(codex_home, sizeof(codex_home), "%s/.codex", home);
    snprintf(codex_day, sizeof(codex_day), "%s/sessions/2026/11/04", codex_home);
    snprintf(codex_file, sizeof(codex_file),
             "%s/rollout-2026-11-04T12-00-00-22222222-2222-4222-8222-222222222222.jsonl",
             codex_day);
    snprintf(codex_workspace, sizeof(codex_workspace), "%s/codex-workspace", root);

    assert(fs_mkdir_p(codex_day) == 0);
    assert(fs_mkdir_p(codex_workspace) == 0);
    char codex_fixture[2048];
    snprintf(codex_fixture, sizeof(codex_fixture),
             "{\"timestamp\":\"2026-11-04T12:00:00Z\",\"type\":\"session_meta\","
             "\"payload\":{\"id\":\"22222222-2222-4222-8222-222222222222\","
             "\"cwd\":\"%s\",\"git\":{\"branch\":\"main\"}}}\n"
             "{\"type\":\"event_msg\",\"payload\":{\"type\":\"user_message\","
             "\"message\":\"Add real Codex discovery\"}}\n",
             codex_workspace);
    write_test_file(codex_file, codex_fixture);
    setenv("CODEX_HOME", codex_home, 1);

    const ai_adapter_t *codex = find_test_adapter("codex");
    assert(codex != NULL);
    flint_config_t codex_cfg;
    config_init_defaults(&codex_cfg);
    session_list_t codex_list;
    session_list_init(&codex_list);
    assert(codex->discover_sessions(&codex_cfg, 24, &codex_list) == 0);
    assert(codex_list.count == 1);
    assert(strcmp(codex_list.items[0].id, "22222222-2222-4222-8222-222222222222") == 0);
    assert(strcmp(codex_list.items[0].workspace, codex_workspace) == 0);
    assert(strcmp(codex_list.items[0].summary, "Add real Codex discovery") == 0);
    assert(strstr(codex_list.items[0].resume_cmd, "codex resume 22222222-2222-4222-8222-222222222222") != NULL);
    session_list_free(&codex_list);
    config_destroy(&codex_cfg);

    unsetenv("CODEX_HOME");
    if (old_codex_home[0] != '\0') {
        setenv("CODEX_HOME", old_codex_home, 1);
    }
    if (old_home[0] != '\0') {
        setenv("HOME", old_home, 1);
    } else {
        unsetenv("HOME");
    }

    char cleanup[MAX_PATH_LEN + 32];
    snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", root);
    assert(system(cleanup) == 0);
    PASS();
}

static void test_openclaw_discovery(void) {
    TEST("openclaw adapter discovery via openclaw sessions");
    const ai_adapter_t **adapters = NULL;
    size_t adapter_count = ai_get_adapters(&adapters);
    int found_openclaw = 0;
    for (size_t i = 0; i < adapter_count; i++) {
        if (adapters[i]->cli_binary && strcmp(adapters[i]->cli_binary, "openclaw") == 0) {
            found_openclaw = 1;
            flint_config_t cfg;
            config_init_defaults(&cfg);
            session_list_t list;
            session_list_init(&list);
            int res = adapters[i]->discover_sessions(&cfg, 24 * 7, &list);
            assert(res == 0);
            printf("       (discovered %zu openclaw session(s))\n", list.count);
            assert(list.count > 0);
            for (size_t s = 0; s < list.count; s++) {
                assert(strstr(list.items[s].resume_cmd, "openclaw resume") != NULL);
            }
            session_list_free(&list);
            config_destroy(&cfg);
            break;
        }
    }
    assert(found_openclaw);
    PASS();
}

static void test_mcp_focus_internals(void) {
    TEST("mcp_query_focus rejects unusable inputs");
    flint_config_t cfg;
    config_init_defaults(&cfg);
    mcp_target_t target;
    memset(&target, 0, sizeof(target));
    char out[512] = {0};

    assert(mcp_query_focus(NULL, &target, "ctx", out, sizeof(out)) == false);
    assert(mcp_query_focus(&cfg, NULL, "ctx", out, sizeof(out)) == false);
    cfg.mcp_focus = false;
    assert(mcp_query_focus(&cfg, &target, "ctx", out, sizeof(out)) == false);
    cfg.mcp_focus = true;
    assert(mcp_query_focus(&cfg, &target, "ctx", out, sizeof(out)) == false);
    config_destroy(&cfg);
    PASS();

    TEST("mcp_query_prompt rejects unusable inputs");
    char out2[512] = {0};
    assert(mcp_query_prompt("/tmp/ws", "ctx", NULL, NULL, out2, sizeof(out2), NULL) == false);
    assert(mcp_query_prompt("/tmp/ws", "ctx", NULL, "instructions", out2, 0, NULL) == false);
    assert(mcp_query_prompt(NULL, "ctx", "instructions", "instructions", out2, sizeof(out2), NULL) == false);
    PASS();

    TEST("mcp_collect_line accumulates text and session id");
    mcp_run_t run;
    memset(&run, 0, sizeof(run));
    const char *line1 = "{\"type\":\"text\",\"part\":{\"type\":\"text\",\"text\":\"- First priority\"}}";
    flint_mcp_collect_line(line1, (int)strlen(line1), &run);
    assert(run.text[0] != '\0');
    assert(strstr(run.text, "First priority") != NULL);
    PASS();

    TEST("mcp_collect_line accumulates multiple text lines");
    mcp_run_t run2;
    memset(&run2, 0, sizeof(run2));
    const char *line2 = "{\"type\":\"text\",\"sessionID\":\"ses_test123\",\"part\":{\"type\":\"text\",\"text\":\"- Second priority\"}}";
    flint_mcp_collect_line(line2, (int)strlen(line2), &run2);
    assert(run2.session_id[0] != '\0');
    assert(strstr(run2.text, "Second priority") != NULL);
    PASS();

    TEST("mcp_collect_line ignores non-text lines");
    mcp_run_t run3;
    memset(&run3, 0, sizeof(run3));
    const char *line3 = "{\"type\":\"agent_message\",\"part\":{\"type\":\"text\",\"text\":\"- Ignored\"}}";
    flint_mcp_collect_line(line3, (int)strlen(line3), &run3);
    assert(run3.text[0] == '\0');
    PASS();
}

static void test_resume_mode_filter(void) {
    TEST("resume_mode filter: user sessions only");
    session_t s;
    memset(&s, 0, sizeof(s));
    s.origin = SESSION_ORIGIN_USER;
    assert(resume_session_should_include(&s, RESUME_MODE_USER) == true);
    assert(resume_session_should_include(&s, RESUME_MODE_ALL) == true);
    assert(resume_session_should_include(&s, RESUME_MODE_AGENT) == false);
    PASS();

    TEST("resume_mode filter: agent sessions");
    memset(&s, 0, sizeof(s));
    s.origin = SESSION_ORIGIN_AGENT;
    assert(resume_session_should_include(&s, RESUME_MODE_AGENT) == true);
    assert(resume_session_should_include(&s, RESUME_MODE_ALL) == true);
    assert(resume_session_should_include(&s, RESUME_MODE_USER) == false);
    PASS();

    TEST("resume_mode filter: all modes");
    memset(&s, 0, sizeof(s));
    s.origin = SESSION_ORIGIN_AGENT;
    assert(resume_session_should_include(&s, RESUME_MODE_ALL) == true);
    PASS();
}

static void test_proc_running(void) {
    TEST("proc_is_session_running exact tool matching");
    /* The test binary itself runs as "test_flint"; an exact (non-substring) match
       on the shorter name "flint" must NOT hit. Regression test for the substring bug. */
    assert(proc_is_session_running("flint", NULL) == false);
    PASS();
}

static void test_journal_generation(void) {
    TEST("journal_record_daily writes daily log");
    char repo[MAX_PATH_LEN];
    snprintf(repo, sizeof(repo), "/tmp/flint_journal_test_%d", (int)getpid());
    flint_config_t cfg;
    config_init_defaults(&cfg);
    strncpy(cfg.journal_repo, repo, sizeof(cfg.journal_repo) - 1);
    cfg.journal_repo[sizeof(cfg.journal_repo) - 1] = '\0';

    session_list_t list;
    session_list_init(&list);
    session_t s;
    memset(&s, 0, sizeof(s));
    strncpy(s.tool_name, "Claude Code", sizeof(s.tool_name) - 1);
    strncpy(s.workspace, "/tmp/flint_ws_test", sizeof(s.workspace) - 1);
    strncpy(s.summary, "Did a thing", sizeof(s.summary) - 1);
    s.origin = SESSION_ORIGIN_USER;
    session_list_add(&list, &s);

    int res = journal_record_daily(&cfg, &list);
    assert(res == 0);

    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    char date_file[32];
    strftime(date_file, sizeof(date_file), "%Y-%m-%d.md", lt);
    char daily_dir[MAX_PATH_LEN];
    fs_path_join(repo, "daily", daily_dir, sizeof(daily_dir));
    char path[MAX_PATH_LEN];
    fs_path_join(daily_dir, date_file, path, sizeof(path));
    assert(fs_file_exists(path));

    session_list_free(&list);
    PASS();

    TEST("journal_generate_weekly writes weekly summary");
    int wres = journal_generate_weekly(&cfg);
    assert(wres == 0);
    char weekly_dir[MAX_PATH_LEN];
    fs_path_join(repo, "weekly", weekly_dir, sizeof(weekly_dir));
    char week_file[64];
    strftime(week_file, sizeof(week_file), "%Y-W%W.md", lt);
    char week_path[MAX_PATH_LEN];
    fs_path_join(weekly_dir, week_file, week_path, sizeof(week_path));
    assert(fs_file_exists(week_path));
    PASS();

    TEST("journal_generate_monthly writes monthly summary");
    int mres = journal_generate_monthly(&cfg);
    assert(mres == 0);
    char monthly_dir[MAX_PATH_LEN];
    fs_path_join(repo, "monthly", monthly_dir, sizeof(monthly_dir));
    char mon_file[64];
    strftime(mon_file, sizeof(mon_file), "%Y-%m.md", lt);
    char mon_path[MAX_PATH_LEN];
    fs_path_join(monthly_dir, mon_file, mon_path, sizeof(mon_path));
    assert(fs_file_exists(mon_path));
    PASS();
}

static void test_git_inspection(void) {
    TEST("git_is_repo on flint workspace");
    assert(git_is_repo("."));

    git_info_t info;
    int res = git_inspect_workspace(".", 24, &info);
    assert(res == 0);
    assert(info.is_git_repo);
    assert(info.branch[0] != '\0');
    assert(info.suggested_focus[0] != '\0');
    PASS();
}

int main(void) {
    printf("\n" ANSI_BOLD "=== Running Flint Unit Tests ===" ANSI_RESET "\n\n");

    test_json_primitives();
    test_json_unescape();
    test_jsonl_scanning();
    test_antigravity_transcript_parsing();
    test_ai_head_parsers();
    test_claude_codex_discovery();
    test_openclaw_discovery();
    test_path_utils();
    test_session_deduplication();
    test_terminal_registry();
    test_config_and_wrappers();
    test_config_file_parsing();
    test_brief_and_mcp();
    test_mcp_focus_internals();
    test_git_inspection();
    test_journal_generation();
    test_proc_running();
    test_resume_mode_filter();

    printf("\n" ANSI_BOLD ANSI_GREEN "All %d tests passed successfully!" ANSI_RESET "\n\n", passed_tests);
    return 0;
}
