#include "terminal.h"
#include "../util/fs.h"
#include <string.h>

#if defined(__APPLE__)

static bool mac_check_app(const char *app_path) {
    return fs_dir_exists(app_path);
}

/* Apple Terminal (with Tab Support) */
static bool apple_terminal_is_available(void) {
    return mac_check_app("/System/Applications/Utilities/Terminal.app") ||
           mac_check_app("/Applications/Utilities/Terminal.app");
}

static int apple_terminal_launch(const session_t *session, bool dry_run) {
    char cmd[MAX_CMD_LEN * 2];
    /* Use AppleScript to open a new tab if Terminal already has a window open */
    snprintf(cmd, sizeof(cmd),
             "osascript -e 'tell application \"Terminal\"' "
             "-e '    if not (exists front window) then' "
             "-e '        do script \"cd \\\"%s\\\" && %s\"' "
             "-e '        activate' "
             "-e '    else' "
             "-e '        activate' "
             "-e '        tell application \"System Events\" to tell process \"Terminal\" to keystroke \"t\" using command down' "
             "-e '        delay 0.15' "
             "-e '        do script \"cd \\\"%s\\\" && %s\" in selected tab of front window' "
             "-e '    end if' "
             "-e 'end tell'",
             session->workspace, session->resume_cmd,
             session->workspace, session->resume_cmd);

    if (dry_run) {
        printf("[Dry Run] Would execute: %s\n", cmd);
        return 0;
    }

    return system(cmd);
}

const terminal_adapter_t term_apple_terminal = {
    .id = "apple-terminal",
    .display_name = "Apple Terminal",
    .os = "macos",
    .is_available = apple_terminal_is_available,
    .launch_session = apple_terminal_launch
};

/* iTerm2 (Native Tab Support) */
static bool iterm2_is_available(void) {
    return mac_check_app("/Applications/iTerm.app") ||
           mac_check_app("~/Applications/iTerm.app");
}

static int iterm2_launch(const session_t *session, bool dry_run) {
    char cmd[MAX_CMD_LEN * 2];
    snprintf(cmd, sizeof(cmd),
             "osascript -e 'tell application \"iTerm2\"' "
             "-e 'if (count of windows) = 0 then' "
             "-e '    create window with default profile' "
             "-e 'else' "
             "-e '    tell current window to create tab with default profile' "
             "-e 'end if' "
             "-e 'tell current session of current window to write text \"cd \\\"%s\\\" && %s\"' "
             "-e 'activate' -e 'end tell'",
             session->workspace, session->resume_cmd);

    if (dry_run) {
        printf("[Dry Run] Would execute: %s\n", cmd);
        return 0;
    }

    return system(cmd);
}

const terminal_adapter_t term_iterm2 = {
    .id = "iterm2",
    .display_name = "iTerm2",
    .os = "macos",
    .is_available = iterm2_is_available,
    .launch_session = iterm2_launch
};

/* Ghostty (Tab Support) */
static bool ghostty_is_available(void) {
    return mac_check_app("/Applications/Ghostty.app") ||
           mac_check_app("~/Applications/Ghostty.app") ||
           system("which ghostty >/dev/null 2>&1") == 0;
}

static int ghostty_launch(const session_t *session, bool dry_run) {
    char cmd[MAX_CMD_LEN * 2];
    /* Try to create tab in running Ghostty using AppleScript, or spawn new instance */
    snprintf(cmd, sizeof(cmd),
             "osascript -e 'if application \"Ghostty\" is running then' "
             "-e '    tell application \"Ghostty\" to activate' "
             "-e '    tell application \"System Events\" to tell process \"Ghostty\" to keystroke \"t\" using command down' "
             "-e '    delay 0.15' "
             "-e '    tell application \"System Events\" to tell process \"Ghostty\" to keystroke \"cd \\\"%s\\\" && %s\\n\"' "
             "-e 'else' "
             "-e '    do shell script \"open -na Ghostty.app --args -e /bin/zsh -c \\\"cd \\\\\\\"%s\\\\\\\" && %s; exec /bin/zsh\\\" >/dev/null 2>&1 &\"' "
             "-e 'end if'",
             session->workspace, session->resume_cmd,
             session->workspace, session->resume_cmd);

    if (dry_run) {
        printf("[Dry Run] Would execute: %s\n", cmd);
        return 0;
    }

    return system(cmd);
}

const terminal_adapter_t term_ghostty = {
    .id = "ghostty",
    .display_name = "Ghostty",
    .os = "macos",
    .is_available = ghostty_is_available,
    .launch_session = ghostty_launch
};

/* Kitty (Native Tab Support via --single-instance) */
static bool kitty_mac_is_available(void) {
    return mac_check_app("/Applications/kitty.app") ||
           system("which kitty >/dev/null 2>&1") == 0;
}

static int kitty_mac_launch(const session_t *session, bool dry_run) {
    char cmd[MAX_CMD_LEN * 2];
    /* Use kitty single instance with launch --type=tab */
    snprintf(cmd, sizeof(cmd),
             "kitty --single-instance --instance-group=flint launch --type=tab --cwd=\"%s\" /bin/zsh -c \"%s; exec /bin/zsh\" >/dev/null 2>&1 &",
             session->workspace, session->resume_cmd);

    if (dry_run) {
        printf("[Dry Run] Would execute: %s\n", cmd);
        return 0;
    }

    return system(cmd);
}

const terminal_adapter_t term_kitty_mac = {
    .id = "kitty",
    .display_name = "Kitty",
    .os = "macos",
    .is_available = kitty_mac_is_available,
    .launch_session = kitty_mac_launch
};

/* Alacritty (macOS) */
static bool alacritty_mac_is_available(void) {
    return mac_check_app("/Applications/Alacritty.app") ||
           system("which alacritty >/dev/null 2>&1") == 0;
}

static int alacritty_mac_launch(const session_t *session, bool dry_run) {
    char cmd[MAX_CMD_LEN * 2];
    snprintf(cmd, sizeof(cmd),
             "alacritty --working-directory \"%s\" -e /bin/zsh -c '%s; exec /bin/zsh' >/dev/null 2>&1 &",
             session->workspace, session->resume_cmd);

    if (dry_run) {
        printf("[Dry Run] Would execute: %s\n", cmd);
        return 0;
    }

    return system(cmd);
}

const terminal_adapter_t term_alacritty_mac = {
    .id = "alacritty",
    .display_name = "Alacritty",
    .os = "macos",
    .is_available = alacritty_mac_is_available,
    .launch_session = alacritty_mac_launch
};

#else

/* Stubs for non-macOS platforms */
static bool mac_stub_available(void) { return false; }
static int mac_stub_launch(const session_t *s, bool d) { (void)s; (void)d; return -1; }

const terminal_adapter_t term_apple_terminal = { "apple-terminal", "Apple Terminal", "macos", mac_stub_available, mac_stub_launch };
const terminal_adapter_t term_iterm2 = { "iterm2", "iTerm2", "macos", mac_stub_available, mac_stub_launch };
const terminal_adapter_t term_ghostty = { "ghostty", "Ghostty", "macos", mac_stub_available, mac_stub_launch };
const terminal_adapter_t term_kitty_mac = { "kitty", "Kitty", "macos", mac_stub_available, mac_stub_launch };
const terminal_adapter_t term_alacritty_mac = { "alacritty", "Alacritty", "macos", mac_stub_available, mac_stub_launch };

#endif
