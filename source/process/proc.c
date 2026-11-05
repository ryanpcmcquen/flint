#include "proc.h"
#include "../util/fs.h"
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
  #include <windows.h>
  #include <tlhelp32.h>
#else
  #include <unistd.h>
#endif

/* Case-insensitive equality check. */
static bool proc_case_eq(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

/* Copy the executable basename from a command line (first token, after any path).
   e.g. "/usr/local/bin/agy --prompt x" -> "agy". */
static void proc_exe_basename(const char *cmdline, char *out, size_t out_size) {
    if (!cmdline || !out || out_size == 0) { if (out) out[0] = '\0'; return; }

    const char *base = cmdline;
    const char *slash = NULL;
    for (const char *p = cmdline; *p; p++) {
        if (*p == '/') slash = p;
    }
    base = slash ? slash + 1 : cmdline;

    size_t len = strcspn(base, " \t");
    if (len > out_size - 1) len = out_size - 1;
    strncpy(out, base, len);
    out[len] = '\0';
}

/* Cached `ps -eo pid,command` output. Flint is a one-shot CLI, so a
   process-lifetime cache avoids spawning ps once per session. The cache grows
   on demand: a fixed small cap silently missed processes on busy machines. */
static char **proc_ps_cache = NULL;
static int proc_ps_cache_count = 0;
static int proc_ps_cache_capacity = 0;

/* Cached `lsof -a -d cwd -p <pid>` results, keyed by pid. */
#define PROC_LSOF_CACHE_MAX 64
static int proc_lsof_pids[PROC_LSOF_CACHE_MAX];
static char proc_lsof_cwds[PROC_LSOF_CACHE_MAX][MAX_PATH_LEN];
static int proc_lsof_count = 0;

static void proc_load_ps_cache(void) {
    if (proc_ps_cache_count > 0) return;

    char cmd[MAX_CMD_LEN * 2];
    snprintf(cmd, sizeof(cmd), "ps -eo pid,command 2>/dev/null");

    FILE *pipe = popen(cmd, "r");
    if (!pipe) return;

    char line[1024];
    proc_ps_cache_count = 0;
    while (fgets(line, sizeof(line), pipe)) {
        if (proc_ps_cache_count >= proc_ps_cache_capacity) {
            int new_capacity = proc_ps_cache_capacity ? proc_ps_cache_capacity * 2 : 256;
            char **grown = (char **)realloc(proc_ps_cache,
                                            (size_t)new_capacity * sizeof(char *));
            if (!grown) break;
            proc_ps_cache = grown;
            proc_ps_cache_capacity = new_capacity;
        }

        size_t len = strlen(line);
        char *entry = (char *)malloc(len + 1);
        if (!entry) break;
        memcpy(entry, line, len + 1);
        proc_ps_cache[proc_ps_cache_count++] = entry;
    }
    pclose(pipe);
}

/* Resolve a process's current working directory via lsof, with caching. */
static const char *proc_lsof_cwd(int pid) {
    for (int i = 0; i < proc_lsof_count; i++) {
        if (proc_lsof_pids[i] == pid) {
            return proc_lsof_cwds[i][0] != '\0' ? proc_lsof_cwds[i] : NULL;
        }
    }

    char cwd_buf[MAX_PATH_LEN] = {0};
    char lsof_cmd[256];
    snprintf(lsof_cmd, sizeof(lsof_cmd), "lsof -a -d cwd -p %d -Fn 2>/dev/null", pid);
    FILE *lpipe = popen(lsof_cmd, "r");
    if (lpipe) {
        char lline[MAX_PATH_LEN];
        while (fgets(lline, sizeof(lline), lpipe)) {
            if (lline[0] == 'n') {
                char *path = lline + 1;
                str_trim(path);
                snprintf(cwd_buf, sizeof(cwd_buf), "%s", path);
                break;
            }
        }
        pclose(lpipe);
    }

    /* Cache the resolved cwd, evicting the oldest slot when the cache is full. */
    int slot;
    if (proc_lsof_count < PROC_LSOF_CACHE_MAX) {
        slot = proc_lsof_count++;
    } else {
        slot = 0;
        for (int i = 1; i < PROC_LSOF_CACHE_MAX; i++) {
            if (proc_lsof_pids[i] == pid) { slot = i; break; }
        }
    }
    proc_lsof_pids[slot] = pid;
    snprintf(proc_lsof_cwds[slot], MAX_PATH_LEN, "%s", cwd_buf);

    if (cwd_buf[0] == '\0') return NULL;
    return proc_lsof_cwds[slot];
}

/* Check a single cached ps line for a running process with the given
   executable basename (exact, case-insensitive) in the given workspace. */
static bool proc_line_matches(const char *line, const char *tool_cmd, const char *workspace_path) {
    int pid = 0;
    char prog[512] = {0};
    if (sscanf(line, "%d %511[^\n]", &pid, prog) < 1) return false;
    if (pid == (int)getpid()) return false;

    char exe[512];
    proc_exe_basename(prog, exe, sizeof(exe));
    if (!proc_case_eq(exe, tool_cmd)) return false;

    if (workspace_path && workspace_path[0] != '\0') {
        if (strstr(prog, workspace_path) != NULL) return true;

        /* Alternatively check cwd of process via cached lsof lookup */
        const char *cwd = proc_lsof_cwd(pid);
        if (cwd && strcmp(cwd, workspace_path) == 0) {
            return true;
        }
    }
    return false;
}

bool proc_is_session_running(const char *tool_cmd, const char *workspace_path) {
    if (!tool_cmd || !*tool_cmd) return false;

#if defined(_WIN32)
    /* Windows check using toolhelp snapshot */
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);
    bool found = false;

    char target_exe[64];
    snprintf(target_exe, sizeof(target_exe), "%s.exe", tool_cmd);

    if (Process32First(hSnapshot, &pe)) {
        do {
            if (strcasecmp(pe.szExeFile, target_exe) == 0 ||
                strcasecmp(pe.szExeFile, tool_cmd) == 0) {
                found = true;
                break;
            }
        } while (Process32Next(hSnapshot, &pe));
    }

    CloseHandle(hSnapshot);
    return found;

#else
    proc_load_ps_cache();

    bool is_running = false;
    for (int i = 0; i < proc_ps_cache_count; i++) {
        if (proc_line_matches(proc_ps_cache[i], tool_cmd, workspace_path)) {
            is_running = true;
            break;
        }
    }

    return is_running;
#endif
}
