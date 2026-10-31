#ifndef FLINT_TERMINAL_H
#define FLINT_TERMINAL_H

#include "../flint.h"

typedef struct terminal_adapter {
    const char *id;             /* CLI identifier: "apple-terminal", "iterm2", "ghostty", "alacritty", "kitty", "gnome-terminal", "konsole", "windows-terminal", "powershell" */
    const char *display_name;   /* Human friendly name */
    const char *os;             /* "macos", "linux", "windows" */
    bool (*is_available)(void);
    int (*launch_session)(const session_t *session, bool dry_run);
} terminal_adapter_t;

/* Get all registered terminal adapters */
size_t terminal_get_all(const terminal_adapter_t ***adapters);

/* Detect current running or default terminal */
const terminal_adapter_t *terminal_detect_default(void);

/* Find terminal by ID or friendly name */
const terminal_adapter_t *terminal_find(const char *name);

/* Print all supported & available terminals */
void terminal_print_list(void);

#endif /* FLINT_TERMINAL_H */
