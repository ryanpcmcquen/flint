#include "terminal.h"
#include "../util/fs.h"
#include <string.h>

/* Forward declare all terminal adapters */
extern const terminal_adapter_t term_apple_terminal;
extern const terminal_adapter_t term_iterm2;
extern const terminal_adapter_t term_ghostty;
extern const terminal_adapter_t term_kitty_mac;
extern const terminal_adapter_t term_alacritty_mac;

extern const terminal_adapter_t term_gnome_terminal;
extern const terminal_adapter_t term_alacritty_linux;
extern const terminal_adapter_t term_kitty_linux;
extern const terminal_adapter_t term_konsole;

extern const terminal_adapter_t term_windows_terminal;
extern const terminal_adapter_t term_powershell;
extern const terminal_adapter_t term_alacritty_win;

static const terminal_adapter_t *all_terminals[] = {
#if defined(__APPLE__)
    &term_iterm2,
    &term_ghostty,
    &term_kitty_mac,
    &term_apple_terminal,
    &term_alacritty_mac
#elif defined(_WIN32)
    &term_windows_terminal,
    &term_powershell,
    &term_alacritty_win
#else
    &term_gnome_terminal,
    &term_alacritty_linux,
    &term_kitty_linux,
    &term_konsole
#endif
};

size_t terminal_get_all(const terminal_adapter_t ***adapters) {
    if (adapters) {
        *adapters = all_terminals;
    }
    return sizeof(all_terminals) / sizeof(all_terminals[0]);
}

const terminal_adapter_t *terminal_detect_default(void) {
#if defined(__APPLE__)
    const char *term_program = getenv("TERM_PROGRAM");
    if (term_program) {
        if (str_contains_ignore_case(term_program, "iterm")) return &term_iterm2;
        if (str_contains_ignore_case(term_program, "apple_terminal")) return &term_apple_terminal;
        if (str_contains_ignore_case(term_program, "ghostty")) return &term_ghostty;
        if (str_contains_ignore_case(term_program, "kitty")) return &term_kitty_mac;
        if (str_contains_ignore_case(term_program, "alacritty")) return &term_alacritty_mac;
    }
    if (getenv("KITTY_PID") || (term_program && str_contains_ignore_case(term_program, "kitty"))) {
        return &term_kitty_mac;
    }
    if (getenv("GHOSTTY_RESOURCES_DIR")) {
        return &term_ghostty;
    }
    if (getenv("ALACRITTY_LOG")) {
        return &term_alacritty_mac;
    }

    /* Fallback preference for macOS: iTerm2 -> Ghostty -> Kitty -> Apple Terminal */
    if (term_iterm2.is_available()) return &term_iterm2;
    if (term_ghostty.is_available()) return &term_ghostty;
    if (term_kitty_mac.is_available()) return &term_kitty_mac;
    if (term_apple_terminal.is_available()) return &term_apple_terminal;
    if (term_alacritty_mac.is_available()) return &term_alacritty_mac;
    return &term_apple_terminal;

#elif defined(_WIN32)
    if (getenv("WT_SESSION")) return &term_windows_terminal;
    if (getenv("ALACRITTY_LOG")) return &term_alacritty_win;

    if (term_windows_terminal.is_available()) return &term_windows_terminal;
    return &term_powershell;

#else
    const char *color_term = getenv("COLORTERM");
    const char *term = getenv("TERM");
    if (getenv("KITTY_PID") || (term && strstr(term, "kitty"))) return &term_kitty_linux;
    if (getenv("ALACRITTY_LOG") || (term && strstr(term, "alacritty"))) return &term_alacritty_linux;
    if (color_term && strstr(color_term, "gnome-terminal")) return &term_gnome_terminal;

    if (term_gnome_terminal.is_available()) return &term_gnome_terminal;
    if (term_kitty_linux.is_available()) return &term_kitty_linux;
    if (term_alacritty_linux.is_available()) return &term_alacritty_linux;
    if (term_konsole.is_available()) return &term_konsole;
    return &term_gnome_terminal;
#endif
}

const terminal_adapter_t *terminal_find(const char *name) {
    if (!name || !*name) return NULL;

    size_t count = sizeof(all_terminals) / sizeof(all_terminals[0]);
    for (size_t i = 0; i < count; i++) {
        const terminal_adapter_t *t = all_terminals[i];
        if (str_contains_ignore_case(t->id, name) ||
            str_contains_ignore_case(t->display_name, name)) {
            return t;
        }
    }
    return NULL;
}

void terminal_print_list(void) {
    printf(ANSI_BOLD "Supported Terminal Clients:\n" ANSI_RESET);
    size_t count = sizeof(all_terminals) / sizeof(all_terminals[0]);
    const terminal_adapter_t *cur = terminal_detect_default();

    for (size_t i = 0; i < count; i++) {
        const terminal_adapter_t *t = all_terminals[i];
        bool avail = t->is_available();
        bool is_current = (cur && strcmp(cur->id, t->id) == 0);

        printf("  - %-18s (%-16s) [%s%s%s]%s\n",
               t->display_name,
               t->id,
               avail ? ANSI_GREEN "Available" ANSI_RESET : ANSI_DIM "Not installed" ANSI_RESET,
               is_current ? ANSI_CYAN ", Current" ANSI_RESET : "",
               "",
               "");
    }
    printf("\n");
}
