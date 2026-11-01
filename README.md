# Flint

> **Start your workday with clarity.**

**Flint** is a fast, dependency-free command-line tool written in C99 designed to be run at the start of your workday. It integrates with modern AI CLI tools (Google **Antigravity CLI**, Anthropic **Claude Code**, OpenAI **ChatGPT Codex**, and more) across **macOS**, **Linux**, and **Windows**.

When invoked in the morning, Flint:
1. **Summarizes accomplishments**: Discovers and synthesizes what was worked on and completed during the previous day across your workspaces and AI tools.
2. **Resumes active sessions**: Automatically re-opens each workspace in your preferred terminal client, spinning up tabs or windows with your active AI sessions right where you left off.

---

## Supported AI CLI Tools

| AI Tool | CLI Binary | Discovery & State Location |
| :--- | :--- | :--- |
| **Antigravity CLI** | `agy` | `~/.gemini/antigravity-cli/brain/` transcripts & logs |
| **Claude Code** | `claude` | `~/.claude.json` projects & `~/.claude/projects/` |
| **ChatGPT Codex** | `codex` | `~/.codex/` session store & project history |

---

## Supported Operating Systems & Terminal Clients

Flint automatically detects your running terminal emulator, or allows you to target one via `--terminal`.

| Platform | Primary Terminals |
| :--- | :--- |
| **macOS** | **Apple Terminal**, **iTerm2**, **Ghostty**, **Alacritty**, **Kitty** |
| **Linux** | **GNOME Terminal**, **Alacritty**, **Kitty**, **Konsole** |
| **Windows** | **Windows Terminal** (`wt.exe`), **PowerShell / ConHost**, **Alacritty** |

---

## Building

Flint uses a single, portable polyglot `Makefile` that compiles with **GNU `make`** on macOS and Linux, as well as Microsoft **`nmake`** on Windows (using Clang or MSVC).

### Linux / macOS
```sh
# Release build (O3)
make

# Run unit tests
make test

# Debug build with symbols
make debug

# AddressSanitizer build
make memdebug

# Clean build artifacts
make clean
```

### Windows (NMAKE)
Open a Developer Command Prompt or PowerShell with `clang` or MSVC in PATH:
```cmd
nmake
```
To run tests on Windows:
```cmd
nmake test
```

---

## Usage

```text
Usage: flint [options]

Options:
  -s, --summary           Display daily summary only (do not launch sessions)
  -r, --resume            Automatically resume all active sessions without prompting
  -t, --terminal <name>   Target terminal client (e.g. iterm2, ghostty, apple-terminal,
                          gnome-terminal, alacritty, kitty, windows-terminal)
  -d, --days <N>          Look back N days (default: 1 day / 24 hours)
      --hours <H>         Look back H hours
  -w, --workspace <path>  Filter sessions to specific workspace directory
  -n, --dry-run           Show commands that would be executed without launching
  -l, --list-terminals    List all supported and detected terminal clients
  -v, --version           Print Flint version
  -h, --help              Print this help menu
```

### Daily Morning Workflow

Simply type `flint` with your morning coffee:

```text
$ flint

  ╔══════════════════════════════════════════════════════════════╗
  ║                        F L I N T                             ║
  ║            Morning Standup Brief & AI Session Manager        ║
  ║                     Monday, November 02, 2026                ║
  ╚══════════════════════════════════════════════════════════════╝

  What was worked on & accomplished:
  ────────────────────────────────────────────────────────────

  [Antigravity CLI] /Users/ryanmcquen/code/flint
    Last active: 2026-10-31 16:45
    ✓ Implemented cross-platform C99 Flint engine; Added terminal adapters
    Resume command: cd "/Users/ryanmcquen/code/flint" && agy

  [Claude Code] /Users/ryanmcquen/code/basque
    Last active: 2026-10-31 15:30
    ✓ Refactored 2D sprite collisions and updated audio mixer
    Resume command: cd "/Users/ryanmcquen/code/basque" && claude

  ────────────────────────────────────────────────────────────

  Resume 2 active session(s) in iTerm2? [Y/n]
```

Pressing **Enter** or typing **`Y`** immediately opens a new tab in your terminal for each project and resumes the active AI CLI.

---

## Architecture & Project Structure

```text
flint/
├── Makefile                 # Polyglot cross-platform Makefile (GNU make + Windows nmake)
├── README.md                # Documentation & quick start guide
├── tests/
│   └── test_flint.c         # Unit test suite for parser, filesystem & terminals
└── source/
    ├── flint.h              # Core data types (session_t, options_t)
    ├── flint.c              # CLI parsing, banner display & standup orchestration
    ├── ai/                  # AI tool adapters
    │   ├── ai_adapter.h     # Adapter interface
    │   ├── antigravity.c    # Antigravity CLI integration
    │   ├── claude.c         # Claude Code integration
    │   └── codex.c          # OpenAI Codex CLI integration
    ├── terminal/            # Terminal launchers & window/tab orchestration
    │   ├── terminal.h       # Terminal adapter interface
    │   ├── terminal.c       # Master registry & detection logic
    │   ├── macos.c          # Apple Terminal, iTerm2, Ghostty, Alacritty, Kitty
    │   ├── linux.c          # GNOME Terminal, Alacritty, Kitty, Konsole
    │   └── windows.c        # Windows Terminal (wt), PowerShell, Alacritty
    └── util/                # Dependency-free C99 utility libraries
        ├── fs.h / fs.c      # Cross-platform filesystem & path utilities
        └── json.h / json.c  # Fast, safe JSON & JSONL tokenizer
```

### Adding a New AI CLI Adapter
Implement the `ai_adapter_t` interface in `source/ai/`:
```c
typedef struct ai_adapter {
    const char *name;
    const char *cli_binary;
    bool (*is_available)(void);
    int (*discover_sessions)(int lookback_hours, session_list_t *list);
} ai_adapter_t;
```

### Adding a New Terminal Client
Implement the `terminal_adapter_t` interface in `source/terminal/`:
```c
typedef struct terminal_adapter {
    const char *id;
    const char *display_name;
    const char *os;
    bool (*is_available)(void);
    int (*launch_session)(const session_t *session, bool dry_run);
} terminal_adapter_t;
```

---

## License

ISC License. See LICENSE for details.
