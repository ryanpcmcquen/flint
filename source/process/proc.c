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
    /* POSIX (macOS & Linux): check ps output for the tool command */
    char cmd[MAX_CMD_LEN * 2];
    snprintf(cmd, sizeof(cmd), "ps -eo pid,command 2>/dev/null");

    FILE *pipe = popen(cmd, "r");
    if (!pipe) return false;

    char line[1024];
    pid_t my_pid = getpid();
    bool is_running = false;

    while (fgets(line, sizeof(line), pipe)) {
        int pid = 0;
        char prog[512] = {0};
        if (sscanf(line, "%d %511[^\n]", &pid, prog) >= 1) {
            if (pid == (int)my_pid) continue;

            /* Check if the command line's executable exactly matches the tool */
            char exe[512];
            proc_exe_basename(prog, exe, sizeof(exe));
            if (proc_case_eq(exe, tool_cmd)) {
                /* If workspace is specified, check if workspace path appears in command args */
                if (workspace_path && workspace_path[0] != '\0') {
                    if (strstr(prog, workspace_path) != NULL) {
                        is_running = true;
                        break;
                    }

                    /* Alternatively check cwd of process via lsof */
                    char lsof_cmd[256];
                    snprintf(lsof_cmd, sizeof(lsof_cmd), "lsof -a -d cwd -p %d -Fn 2>/dev/null", pid);
                    FILE *lpipe = popen(lsof_cmd, "r");
                    if (lpipe) {
                        char lline[MAX_PATH_LEN];
                        while (fgets(lline, sizeof(lline), lpipe)) {
                            if (lline[0] == 'n') {
                                char *path = lline + 1;
                                str_trim(path);
                                if (strcmp(path, workspace_path) == 0) {
                                    is_running = true;
                                    break;
                                }
                            }
                        }
                        pclose(lpipe);
                        if (is_running) break;
                    }
                }
            }
        }
    }

    pclose(pipe);
    return is_running;
#endif
}
