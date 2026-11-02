#include "proc.h"
#include "../util/fs.h"
#include <string.h>

#ifdef _WIN32
  #include <windows.h>
  #include <tlhelp32.h>
#else
  #include <unistd.h>
#endif

bool proc_is_session_running(const char *tool_cmd, const char *workspace_path) {
    if (!tool_cmd || !*tool_cmd) return false;

#ifdef _WIN32
    /* Windows check using toolhelp snapshot */
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);
    bool found = false;

    char target_exe[MAX_PATH_LEN];
    snprintf(target_exe, sizeof(target_exe), "%s.exe", tool_cmd);

    if (Process32First(hSnapshot, &pe)) {
        do {
            /* Match on exact executable name (case-insensitive), including path */
            if (strcasecmp(pe.szExeFile, target_exe) == 0 ||
                strcasecmp(pe.szExeFile, tool_cmd) == 0) {
                /* Verify with workspace if provided */
                if (!workspace_path || workspace_path[0] == '\0') {
                    found = true;
                    break;
                }
                /* Check if the process command line contains the workspace path */
                char cmdline[MAX_PATH_LEN * 2];
                if (GetModuleFileNameA(NULL, cmdline, sizeof(cmdline)) > 0) {
                    // Convert to lowercase for case-insensitive comparison
                    char cmdline_lower[MAX_PATH_LEN * 2];
                    char *p = cmdline_lower;
                    for (char *c = cmdline; *c && p < cmdline_lower + sizeof(cmdline_lower) - 1; c++, p++) {
                        *p = (char)tolower((unsigned char)*c);
                    }
                    *p = '\0';

                    char workspace_lower[MAX_PATH_LEN * 2];
                    char *p2 = workspace_lower;
                    for (char *c = workspace_path; *c && p2 < workspace_lower + sizeof(workspace_lower) - 1; c++, p2++) {
                        *p2 = (char)tolower((unsigned char)*c);
                    }
                    *p2 = '\0';

                    if (strstr(cmdline_lower, workspace_lower) != NULL) {
                        found = true;
                        break;
                    }
                }
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

            /* Check if command line contains the tool name (as a whole word, not substring) */
            if (strstr(prog, tool_cmd) != NULL) {
                /* If workspace is specified, check if workspace path appears in command args */
                if (!workspace_path || workspace_path[0] == '\0') {
                    is_running = true;
                    break;
                }

                /* First check if the workspace appears in command args */
                if (strstr(prog, workspace_path) != NULL) {
                    is_running = true;
                    break;
                }

                /* Alternatively check cwd of process via lsof */
                char lsof_cmd[MAX_CMD_LEN];
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

    pclose(pipe);
    return is_running;
#endif
}
