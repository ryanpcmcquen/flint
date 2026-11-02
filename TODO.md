# Flint TODO

Status reviewed against the code on 2026-11-02. `make` builds cleanly and `make test` passes (25/25).

## Original items

- [ ] **Resume the session (don't just relaunch).** NOT DONE.
  - `config_format_tool_cmd()` in `source/config/config.c` takes `session_id` but ignores it (`(void)session_id;`), so every resume command is just `[wrapper] agy --model ... --effort ...`, which starts a fresh session.
  - `agy` has `--conversation <ID>` ("Resume a previous conversation by ID") and `--continue`. I confirmed this with `agy --help`. Antigravity's `session.id` is the brain directory name, so it should be usable as the ID. Not yet tested against a real resume.
  - Claude and Codex: neither CLI is installed on this machine, so I couldn't check their flags. Look up the real resume syntax before wiring them up. From memory, and unverified, Claude has `--resume`/`--continue` and Codex has a `resume` subcommand.
  - Claude and Codex session IDs aren't real session IDs yet. `claude.c` uses the project directory name or the placeholder `"claude-session"`. `codex.c` uses the top-level entry name in `~/.codex`. Both need real discovery of session files (e.g. the `.jsonl` file names) before an ID-based resume can work.
  - Add a test for the resume command string. The existing test only checks the `graperoot agy` prefix and the `--model`/`--effort` flags.
- [x] **Make the inclusion of GrapeRoot configurable.** DONE.
  - Generalized into a full tool list: `flint_config_t` now holds an ordered `tool_configs` list instead of three fixed per-tool structs (`source/flint.h`). `config_add_tool()` / `config_find_tool()` manage it.
  - Config file sections `[agy]`, `[claude]`, `[codex]` (aliases `[antigravity]`, `[openai]` accepted) set `wrapper`, `model` and `effort`.
  - `wrapper` is **empty by default** — no hardcoded `graperoot` launcher anymore, and the `--init-config` template documents that an empty `wrapper` means "run the raw CLI".
  - Testing: new tests in `tests/test_flint.c` (lookup, format with/without wrapper, config file parsing for `[agy]` and `[codex]` alias sections). `make test` → 17/17.
  - End-to-end: `flint --init-config` writes a working template; `flint -c <file> --summary --dry-run` resolves the matching tool and prepends the configured wrapper to the resume command.
  - Still open: a CLI override like `--wrapper <name>` isn't wired to `parse_args()`; the README has no `wrapper`/tool-config section yet.
- [ ] **Verify the code is as performant as possible.** NOT DONE. No profiling or benchmarking has been done.
  - Rough timing: `flint --summary --days 30` took about 0.14s and `--hours 1` about 0.04s, with only 1 session on this machine. That says little about a larger setup.
  - Likely hotspots, from reading the code. None are measured yet:
    - `proc_is_session_running()` runs `ps -eo pid,command` once per session, plus an `lsof` per matching process. Run `ps` once and reuse the result.
    - `git_inspect_workspace()` spawns 3 `git` subprocesses per session (`rev-parse`, `status`, `log`). `git status --porcelain -b` could replace two of them.
    - `parse_transcript_line()` in `antigravity.c` mallocs and copies every line.
    - `session_list_add()` dedupes with a linear scan. This is fine for small lists.
  - Profile with a realistic number of sessions before optimizing.
- [x] **Make the amount of hours to check for active sessions configurable.** DONE.
  - Config key `lookback_hours` (default 24), CLI `--hours <H>` and `-d/--days <N>`. The CLI overrides the config (`flint.c`, `main()`).
  - I ran it with a `lookback_hours` config file and the CLI flags. The sample data was inconclusive: the one transcript was 53 minutes old, which fits inside the 1-hour window. The override logic is straightforward when reading the code.
  - Small leftovers:
    - `atoi()` silently ignores invalid input, so `--hours abc` is a no-op.
    - The README doesn't document the config key.

## Other issues found while reviewing

- [x] `config_create_default_file()` opens the file with `"w"` and never checks whether it exists. Fixed: it now returns false when the target already exists, matching its header contract.
- [ ] `--init-config` shells out to `mkdir -p ... 2>/dev/null`, which won't work on Windows. Use a portable mkdir helper in `util/fs`.
- [x] README is out of date. Addressed in the Scribe/MCP/brief work: added MCP/messaging section, `mcp_focus`/`--mcp`/`--no-mcp`, brief/focus sections, journal + configuration docs, and completed the usage block (`--origin`, `-j/--journal`, `--weekly`, `--monthly`, `--mcp`, `--no-mcp`, `-c`, `--init-config`). Remaining: full config-file format reference.
- [ ] Antigravity falls back to `~` as the workspace when it can't find one. Check the workspace extraction (`Active Workspaces:` / `Command Working Directory:` parsing).
- [ ] Claude and Codex discovery looks like placeholder logic. Claude decodes `%2F` in directory names, and Codex treats top-level `~/.codex` entries as sessions with workspace `~`. I couldn't verify either against real data, since neither tool's data directory exists here. Check against real installs.
- [x] `proc_is_session_running()` matches `strstr(prog, tool_cmd)`, so a short name like `agy` can match unrelated command lines. Fixed: POSIX branch now extracts the executable basename from the first command token (after any path) and compares it to the tool name case-insensitively; workspace fallback (lsof cwd check) unchanged. Windows branch already compared exact exe names (`tool_cmd` vs `tool_cmd.exe`) and ignores the workspace — left as a known limitation.
- [ ] Test coverage is thin. Nothing covers config file parsing, `proc`, `journal`, the AI adapters, or `resume_mode` filtering.
- [ ] Housekeeping: untracked `flint.dSYM/` and the `flint` binary are ignored via `.gitignore`. Commit this TODO.md.