#ifndef FLINT_TERMINAL_H
#define FLINT_TERMINAL_H

#include "../flint.h"

typedef struct terminal_adapter {
    /* CLI identifier: "apple-terminal", "iterm2", "ghostty", "alacritty", "kitty", "gnome-terminal", "konsole", "windows-terminal", "powershell" */
    const char *id;
    /* Human friendly name */
    const char *display_name;
    /* "macos", "linux", "windows" */
    const char *os;
    bool (*is_available)(void);
    int (*launch_session)(const session_t *session, bool dry_run, bool open_in_tabs);
} terminal_adapter_t;

/* Get all registered terminal adapters */
size_t terminal_get_all(const terminal_adapter_t ***adapters);

/* Detect current running or default terminal */
const terminal_adapter_t *terminal_detect_default(void);

/* Find terminal by ID or friendly name */
const terminal_adapter_t *terminal_find(const char *name);

/* Print all supported & available terminals */
void terminal_print_list(void);

#endif
