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

            /* Check if command line contains the tool name */
            if (strstr(prog, tool_cmd) != NULL) {
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
