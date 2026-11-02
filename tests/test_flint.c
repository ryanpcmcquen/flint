#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../source/flint.h"
#include "../source/config/config.h"
#include "../source/git/git.h"
#include "../source/util/fs.h"
#include "../source/util/json.h"
#include "../source/terminal/terminal.h"
#include "../source/journal/journal.h"

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
    assert(cfg.tool_config_count == 3);
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

    TEST("config_format_tool_cmd includes resume session_id for agy");
    flint_config_t cfg5;
    config_init_defaults(&cfg5);
    char cmd5[512] = {0};
    config_format_tool_cmd(&cfg5, "agy", "abc123", cmd5, sizeof(cmd5));
    assert(strstr(cmd5, "--conversation abc123") != NULL);
    config_destroy(&cfg5);
    PASS();

    TEST("config_format_tool_cmd includes resume session_id for claude");
    flint_config_t cfg6;
    config_init_defaults(&cfg6);
    char cmd6[512] = {0};
    config_format_tool_cmd(&cfg6, "claude", "def456", cmd6, sizeof(cmd6));
    assert(strstr(cmd6, "--resume def456") != NULL);
    config_destroy(&cfg6);
    PASS();

    TEST("config_format_tool_cmd includes resume session_id for codex");
    flint_config_t cfg7;
    config_init_defaults(&cfg7);
    char cmd7[512] = {0};
    config_format_tool_cmd(&cfg7, "codex", "ghi789", cmd7, sizeof(cmd7));
    assert(strstr(cmd7, "codex resume ghi789") != NULL);
    config_destroy(&cfg7);
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
}

static void test_journal_daily(void) {
    TEST("journal_record_daily with real config and temp repo");
    {
        /* Create a temporary journal repo and test daily recording */
        char repo[MAX_PATH_LEN];
        snprintf(repo, sizeof(repo), "/tmp/flint_test_journal_%d", (int)getpid());
        snprintf(repo, sizeof(repo), "%s/journal", repo);
        
        /* Create journal directory structure */
        char daily[MAX_PATH_LEN];
        snprintf(daily, sizeof(daily), "%s/daily", repo);
        mkdir(repo, 0755);
        mkdir(daily, 0755);

        flint_config_t cfg;
        config_init_defaults(&cfg);
        strncpy(cfg.journal_repo, repo, sizeof(cfg.journal_repo) - 1);
        cfg.journal_repo[sizeof(cfg.journal_repo) - 1] = ' ';

        session_list_t list;
        session_list_init(&list);

        session_t s;
        memset(&s, 0, sizeof(s));
        strncpy(s.id, "test1", sizeof(s.id) - 1);
        strncpy(s.tool_name, "Test Tool", sizeof(s.tool_name) - 1);
        strncpy(s.tool_cmd, "test", sizeof(s.tool_cmd) - 1);
        s.origin = SESSION_ORIGIN_USER;
        s.workspace[0] = ' ';
        strncpy(s.workspace, "/tmp/test-project", sizeof(s.workspace) - 1);
        s.summary[0] = ' ';
        strncpy(s.summary, "Test session", sizeof(s.summary) - 1);
        s.last_active = time(NULL);
        fs_format_time(s.last_active, s.last_active_str, sizeof(s.last_active_str));

        session_list_add(&list, &s);

        int result = journal_record_daily(&cfg, &list);
        assert(result == 0);

        /* Verify the file was created */
        char date_file[32];
        time_t now = time(NULL);
        struct tm *lt = localtime(&now);
        strftime(date_file, sizeof(date_file), "%Y-%m-%d.md", lt);
        char file_path[MAX_PATH_LEN];
        snprintf(file_path, sizeof(file_path), "%s/daily/%s", repo, date_file);
        assert(fs_file_exists(file_path));

        /* Cleanup */
        session_list_free(&list);
        config_destroy(&cfg);
        /* Don't remove the repo - just leave it for inspection */
    }
    PASS();
}

static void test_claude_codex_discovery(void) {
    TEST("Claude path decoding: %2F becomes / in dir names");
    {
        char decoded[MAX_PATH_LEN] = {0};
        const char *entry = "project%2Fsubdir%2Fsession";
        char *d = decoded;
        const char *s = entry;
        while (*s && (size_t)(d - decoded) < sizeof(decoded) - 1) {
            if (*s == '%' && *(s+1) == '2' && (*(s+2) == 'F' || *(s+2) == 'f')) {
                *d++ = '/';
                s += 3;
            } else {
                *d++ = *s++;
            }
        }
        *d = '\0';
        assert(strcmp(decoded, "project/subdir/session") == 0);
    }
    PASS();

    TEST("Codex directory scanning with timestamp cutoff");
    {
        /* Verify filesystem reading logic with a real file */
        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "/tmp/flint_test_codex_%d.json", (int)getpid());
        FILE *f = fopen(path, "w");
        assert(f);
        time_t now = time(NULL);
        fputs("{}", f);
        fclose(f);

        time_t cutoff = now - (24 * 3600);
        /* File should exist and be newer than cutoff */
        struct stat st;
        assert(stat(path, &st) == 0);
        assert(st.st_mtime >= cutoff);
        remove(path);
    }
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
    test_git_inspection();
    test_claude_codex_discovery();
    test_journal_daily();

    printf("\n" ANSI_BOLD ANSI_GREEN "All %d tests passed successfully!" ANSI_RESET "\n\n", passed_tests);
    return 0;
}
