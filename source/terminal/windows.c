#include "terminal.h"
#include <string.h>

#if defined(_WIN32)
#include <windows.h>

static bool win_has_cmd(const char *exe_name) {
    char find_cmd[256];
    snprintf(find_cmd, sizeof(find_cmd), "where %s >nul 2>&1", exe_name);
    return system(find_cmd) == 0;
}

/* Windows Terminal */
static bool wt_is_available(void) {
    if (getenv("WT_SESSION")) return true;
    return win_has_cmd("wt.exe");
}

static int wt_launch(const session_t *session, bool dry_run) {
    char cmd[MAX_CMD_LEN * 2];
    /* wt.exe -w 0 new-tab -d "path" cmd /k "resume_cmd" */
    snprintf(cmd, sizeof(cmd),
             "wt.exe -w 0 new-tab -d \"%s\" cmd.exe /k \"%s\"",
             session->workspace, session->resume_cmd);

    if (dry_run) {
        printf("[Dry Run] Would execute: %s\n", cmd);
        return 0;
    }

    return system(cmd);
}

const terminal_adapter_t term_windows_terminal = {
    .id = "windows-terminal",
    .display_name = "Windows Terminal",
    .os = "windows",
    .is_available = wt_is_available,
    .launch_session = wt_launch
};

/* PowerShell / ConHost */
static bool powershell_is_available(void) {
    return true; /* Native on all modern Windows installations */
}

static int powershell_launch(const session_t *session, bool dry_run) {
    char cmd[MAX_CMD_LEN * 2];
    snprintf(cmd, sizeof(cmd),
             "start powershell.exe -NoExit -Command \"Set-Location '%s'; %s\"",
             session->workspace, session->resume_cmd);

    if (dry_run) {
        printf("[Dry Run] Would execute: %s\n", cmd);
        return 0;
    }

    return system(cmd);
}

const terminal_adapter_t term_powershell = {
    .id = "powershell",
    .display_name = "PowerShell / Console",
    .os = "windows",
    .is_available = powershell_is_available,
    .launch_session = powershell_launch
};

/* Alacritty (Windows) */
static bool alacritty_win_is_available(void) {
    return win_has_cmd("alacritty.exe");
}

static int alacritty_win_launch(const session_t *session, bool dry_run) {
    char cmd[MAX_CMD_LEN * 2];
    snprintf(cmd, sizeof(cmd),
             "start alacritty.exe --working-directory \"%s\" -e cmd.exe /k \"%s\"",
             session->workspace, session->resume_cmd);

    if (dry_run) {
        printf("[Dry Run] Would execute: %s\n", cmd);
        return 0;
    }

    return system(cmd);
}

const terminal_adapter_t term_alacritty_win = {
    .id = "alacritty",
    .display_name = "Alacritty (Windows)",
    .os = "windows",
    .is_available = alacritty_win_is_available,
    .launch_session = alacritty_win_launch
};

#else

/* Stubs for non-Windows */
static bool win_stub_available(void) { return false; }
static int win_stub_launch(const session_t *s, bool d) { (void)s; (void)d; return -1; }

const terminal_adapter_t term_windows_terminal = { "windows-terminal", "Windows Terminal", "windows", win_stub_available, win_stub_launch };
const terminal_adapter_t term_powershell = { "powershell", "PowerShell", "windows", win_stub_available, win_stub_launch };
const terminal_adapter_t term_alacritty_win = { "alacritty", "Alacritty (Windows)", "windows", win_stub_available, win_stub_launch };

#endif
