#include "terminal.h"
#include <string.h>

#if defined(__linux__) || (defined(__unix__) && !defined(__APPLE__))

static bool linux_has_bin(const char *bin) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", bin);
    return system(cmd) == 0;
}

/* GNOME Terminal */
static bool gnome_terminal_is_available(void) {
    return linux_has_bin("gnome-terminal");
}

static int gnome_terminal_launch(const session_t *session, bool dry_run) {
    char cmd[MAX_CMD_LEN * 2];
    snprintf(cmd, sizeof(cmd),
             "gnome-terminal --tab --working-directory=\"%s\" -- bash -c \"%s; exec bash\" >/dev/null 2>&1 &",
             session->workspace, session->resume_cmd);

    if (dry_run) {
        printf("[Dry Run] Would execute: %s\n", cmd);
        return 0;
    }

    return system(cmd);
}

const terminal_adapter_t term_gnome_terminal = {
    .id = "gnome-terminal",
    .display_name = "GNOME Terminal",
    .os = "linux",
    .is_available = gnome_terminal_is_available,
    .launch_session = gnome_terminal_launch
};

/* Alacritty (Linux) */
static bool alacritty_linux_is_available(void) {
    return linux_has_bin("alacritty");
}

static int alacritty_linux_launch(const session_t *session, bool dry_run) {
    char cmd[MAX_CMD_LEN * 2];
    snprintf(cmd, sizeof(cmd),
             "alacritty --working-directory \"%s\" -e bash -c \"%s; exec bash\" >/dev/null 2>&1 &",
             session->workspace, session->resume_cmd);

    if (dry_run) {
        printf("[Dry Run] Would execute: %s\n", cmd);
        return 0;
    }

    return system(cmd);
}

const terminal_adapter_t term_alacritty_linux = {
    .id = "alacritty",
    .display_name = "Alacritty",
    .os = "linux",
    .is_available = alacritty_linux_is_available,
    .launch_session = alacritty_linux_launch
};

/* Kitty (Linux) */
static bool kitty_linux_is_available(void) {
    return linux_has_bin("kitty");
}

static int kitty_linux_launch(const session_t *session, bool dry_run) {
    char cmd[MAX_CMD_LEN * 2];
    snprintf(cmd, sizeof(cmd),
             "kitty --directory \"%s\" bash -c \"%s; exec bash\" >/dev/null 2>&1 &",
             session->workspace, session->resume_cmd);

    if (dry_run) {
        printf("[Dry Run] Would execute: %s\n", cmd);
        return 0;
    }

    return system(cmd);
}

const terminal_adapter_t term_kitty_linux = {
    .id = "kitty",
    .display_name = "Kitty",
    .os = "linux",
    .is_available = kitty_linux_is_available,
    .launch_session = kitty_linux_launch
};

/* Konsole */
static bool konsole_is_available(void) {
    return linux_has_bin("konsole");
}

static int konsole_launch(const session_t *session, bool dry_run) {
    char cmd[MAX_CMD_LEN * 2];
    snprintf(cmd, sizeof(cmd),
             "konsole --workdir \"%s\" -e bash -c \"%s; exec bash\" >/dev/null 2>&1 &",
             session->workspace, session->resume_cmd);

    if (dry_run) {
        printf("[Dry Run] Would execute: %s\n", cmd);
        return 0;
    }

    return system(cmd);
}

const terminal_adapter_t term_konsole = {
    .id = "konsole",
    .display_name = "Konsole",
    .os = "linux",
    .is_available = konsole_is_available,
    .launch_session = konsole_launch
};

#else

/* Stubs for non-Linux */
static bool linux_stub_available(void) { return false; }
static int linux_stub_launch(const session_t *s, bool d) { (void)s; (void)d; return -1; }

const terminal_adapter_t term_gnome_terminal = { "gnome-terminal", "GNOME Terminal", "linux", linux_stub_available, linux_stub_launch };
const terminal_adapter_t term_alacritty_linux = { "alacritty", "Alacritty", "linux", linux_stub_available, linux_stub_launch };
const terminal_adapter_t term_kitty_linux = { "kitty", "Kitty", "linux", linux_stub_available, linux_stub_launch };
const terminal_adapter_t term_konsole = { "konsole", "Konsole", "linux", linux_stub_available, linux_stub_launch };

#endif
