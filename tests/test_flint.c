#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "../source/flint.h"
#include "../source/ai/mcp.h"
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
    assert(cfg.tool_config_count == 5);
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
    test_path_utils();
    test_session_deduplication();
    test_terminal_registry();
    test_config_and_wrappers();
    test_config_file_parsing();
    test_brief_and_mcp();
    test_git_inspection();
    test_journal_generation();
    test_proc_running();

    printf("\n" ANSI_BOLD ANSI_GREEN "All %d tests passed successfully!" ANSI_RESET "\n\n", passed_tests);
    return 0;
}
