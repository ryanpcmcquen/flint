# Flint

![Flint](flint_logo.jpeg)

> **Start your workday with clarity.**

**Flint** is a fast, dependency-free command-line tool written in C99 designed to be run at the start of your workday. It integrates with modern AI CLI tools (Google **Antigravity CLI**, Anthropic **Claude Code**, OpenAI **ChatGPT Codex**, OpenClaw, and more) across **macOS**, **Linux**, and **Windows**.

When invoked in the morning, Flint:
1. **Presents a morning brief**: Groups recent work by workspace and summarizes what was accomplished across your AI tools.
2. **Suggests a focus for today**: Ranks where work was left unfinished, starting with uncommitted changes and recent commits.
3. **Checks connected tools**: If MCP servers such as Slack are connected through OpenCode, Flint runs a read-only query to help prioritize the day.
4. **Resumes active sessions**: Automatically re-opens each workspace in your preferred terminal client, spinning up tabs or windows with your active AI sessions right where you left off.

---

## Supported AI CLI Tools

| AI Tool | CLI Binary | Discovery & State Location |
| :--- | :--- | :--- |
| **Antigravity CLI** | `agy` | `~/.gemini/antigravity-cli/brain/` transcripts & logs |
| **Claude Code** | `claude` | `~/.claude/projects/<project>/<session-id>.jsonl` transcripts |
| **ChatGPT Codex** | `codex` | `$CODEX_HOME/sessions/YYYY/MM/DD/rollout-*.jsonl` (default `~/.codex`) |
| **Cline** | `cline` | `~/.cline/data/sessions/` session metadata & messages |
| **OpenCode** | `opencode` | Local OpenCode session API (`opencode api session.list`) |
| **OpenClaw** | `openclaw` | `openclaw sessions --json` session store & `openclaw resume <key>` |

### Messaging Clients & MCP Tools

AI chat inside messaging clients (Slack, Microsoft Teams, Discord, WhatsApp, and more) is supported through **connected MCP servers**. Flint checks for connected MCP servers via `opencode api mcp.list`, then runs a read-only OpenCode agent prompt that combines your recent work context with messages from the connected platforms. The returned priorities are merged into the ranked “Focus for today” list.

Set `mcp_focus = false` in `~/.config/flint/config` to disable this feature.

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
      --wrapper <name>    Prepend this launcher to every resume command
  -d, --days <N>          Look back N days (default: 1 day / 24 hours)
      --hours <H>         Look back H hours
  -w, --workspace <path>  Filter sessions to specific workspace directory
  -j, --journal           Record daily standup log to markdown Git repository
      --weekly            Generate and display weekly standup summary
      --monthly           Generate and display monthly standup summary
      --mcp               Query connected MCP tools for today's focus
      --no-mcp            Skip MCP queries for this run
  -c, --config <path>     Path to custom flint configuration file
      --init-config       Generate default ~/.config/flint/config template
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
  ║                          F L I N T                           ║
  ║          Morning Standup Brief & AI Session Manager          ║
  ║                  Monday, November 02, 2026                   ║
  ╚══════════════════════════════════════════════════════════════╝

  Morning Brief
  ────────────────────────────────────────────────────────────
  In the last 24 hours you worked in 2 workspaces across 3 AI sessions using Antigravity CLI, Claude Code.

  flint ~/code/flint
    Claude Code · Refactored the session adapter registry  09:41
    Antigravity CLI · Added cross-platform terminal adapters  09:12
    Git: main · 4 change(s) [2 modified, 0 staged, 2 untracked]

  basque ~/code/basque
    Claude Code · Updated the audio mixer and sprite collisions  08:52

  Focus for today
  ────────────────────────────────────────────────────────────
    1. flint — Finish uncommitted changes in source.json
    2. basque — Pick up: Updated the audio mixer and sprite collisions

  Session details
  ────────────────────────────────────────────────────────────

  [Antigravity CLI] ~/code/flint  (User)
    Last active: 2026-11-02 09:12
    ✓ Added cross-platform terminal adapters
    Resume command: cd "~/code/flint" && agy

  [Claude Code] ~/code/basque  (User)
    Last active: 2026-11-02 08:52
    ✓ Updated the audio mixer and sprite collisions
    Resume command: cd "~/code/basque" && claude

  ────────────────────────────────────────────────────────────

  Resume 2 active session(s) in iTerm2? [Y/n]
```

Pressing **Enter** or typing **`Y`** immediately opens a new tab in your terminal for each project and resumes the active AI CLI.

### Daily Standup Log & Journal

Pass `-j` / `--journal` to record the daily standup summary into a markdown Git repository:

```text
$ flint --journal
  ✓ Daily standup logged to ~/notes/flint-journal/daily/2026-11-02.md
```

Use `--weekly` to compile the last 7 daily logs into a weekly summary, or `--monthly` for the last 30.

The journal repository is configured with `journal_repo` in the config file (default: `~/notes/flint-journal`).

### Configuration

Flint reads `~/.config/flint/config` (or `~/.flintrc`). Run `flint --init-config` to create a template.

Global configuration keys:

| Key | Default | Description |
| :--- | :--- | :--- |
| `terminal` | (auto) | Preferred terminal client |
| `resume_mode` | `user` | `user`, `all`, or `agent` |
| `journal_repo` | `~/notes/flint-journal` | Markdown journal / work-log repository |
| `lookback_hours` | `24` | Hours to look back for sessions |
| `open_in_tabs` | `true` | Open resumes in tabs; `false` opens new windows |
| `auto_journal` | `false` | Auto-record daily log |
| `mcp_focus` | `true` | Query connected MCP tools for focus |

MCP focus queries are enabled by default. Disable them globally with `mcp_focus = false`, or per run with `--no-mcp`. Flint only queries when OpenCode reports at least one connected MCP server, and the query is read-only. Any priorities returned by the connected tools are merged into the ranked "Focus for today" list, and connected messaging clients such as Slack or Microsoft Teams are covered automatically through their MCP servers.

### Full Configuration File Format

The `[tool]` sections of `~/.config/flint/config` (friendly aliases such as `[antigravity]`, `[openai]`, `[open-code]` are accepted):

```ini
[agy]
wrapper =
model = gemini-3.8-flash
effort = high

[claude]
wrapper =
model = claude-3-7-sonnet
effort = high

[codex]
wrapper =
model = o3-mini
effort = high

[cline]
wrapper =
model =
effort =

[opencode]
wrapper =
model =
effort =

[openclaw]
wrapper =
model =
effort =
```

- `wrapper` — launcher binary prepended to the resume command (e.g. `graperoot`); blank means run the raw CLI. The CLI flag `--wrapper <name>` overrides this for every tool at runtime.
- `model` / `effort` — model and effort passed to the AI CLI.

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
    │   ├── cline.c          # Cline session store integration
    │   ├── codex.c          # OpenAI Codex CLI integration
    │   ├── mcp.c / mcp.h    # Connected MCP detection & read-only focus query
    │   └── opencode.c       # OpenCode local API integration
    │   └── openclaw.c       # OpenClaw CLI session store & resume integration
    ├── brief/               # Grouped morning brief & ranked focus
    │   └── brief.c / brief.h
    ├── terminal/            # Terminal launchers & window/tab orchestration
    │   ├── terminal.h       # Terminal adapter interface
    │   ├── terminal.c       # Master registry & detection logic
    │   ├── macos.c          # Apple Terminal, iTerm2, Ghostty, Alacritty, Kitty
    │   ├── linux.c          # GNOME Terminal, Alacritty, Kitty, Konsole
    │   └── windows.c        # Windows Terminal (wt), PowerShell, Alacritty
    └── util/                # Dependency-free C99 utility libraries
        ├── exec.h / exec.c  # Shell capture, command detection & quoting
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
    int (*discover_sessions)(const flint_config_t *cfg, int lookback_hours,
                             session_list_t *list);
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
    int (*launch_session)(const session_t *session, bool dry_run, bool open_in_tabs);
} terminal_adapter_t;
```

---
