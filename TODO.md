# Flint TODO

Status reviewed against the code on 2026-11-02. `make` builds cleanly and `make test` passes (26/26).

## Original items

- [ ] **Resume the session (don't just relaunch).** IN PROGRESS. Resume command wiring completed this round: `agy` emits ` --conversation <ID>`, `claude` emits ` --resume <ID>`, and `codex` emits ` resume <ID>` when a session id is present (tests added; `make test` 28/28). PR #1 conflicts resolved via merge of `origin/main` (merge commit `8a0e677`); resume end-to-end still blocked on real CLI installs / session ids.
  - `config_format_tool_cmd()` in `source/config/config.c` takes `session_id` but the `agy` branch ignored it. Fixed: the `agy` branch now emits ` --conversation <session_id>` when a session id is present, so the resume command becomes `[wrapper] agy --conversation <ID> --model ... --effort ...`. The `claude` branch now emits ` --resume <session_id>` and the `codex` branch emits ` resume <session_id>` when a session id is present.
  - `agy` has `--conversation <ID>` ("Resume a previous conversation by ID") and `--continue`. I confirmed this with `agy --help`. Antigravity's `session.id` is the brain directory name, so it should be usable as the ID. Not yet tested against a real resume.
  - Claude and Codex: neither CLI is installed on this machine, so I couldn't check their flags. Look up the real resume syntax before wiring them up. From memory, and unverified, Claude has `--resume`/`--continue` and Codex has a `resume` subcommand. Resume commands were wired anyway (`claude --resume <id>`, `codex resume <id>`) with tests added; flags still need real-CLI verification.
  - Claude and Codex session IDs aren't real session IDs yet. `claude.c` uses the project directory name or the placeholder `"claude-session"`. `codex.c` uses the top-level entry name in `~/.codex`. Both need real discovery of session files (e.g. the `.jsonl` file names) before an ID-based resume can work.
  - Add a test for the resume command string. DONE: added `config_format_tool_cmd resumes Antigravity sessions via --conversation`, `... resumes OpenCode sessions`, `... resumes Claude sessions via --resume`, and `... resumes Codex sessions via resume subcommand` tests in `tests/test_flint.c`; `make test` now passes 28/28. End-to-end resume still needs real CLI installs.
- [x] **Make the inclusion of GrapeRoot configurable.** DONE.
  - Generalized into a full tool list: `flint_config_t` now holds an ordered `tool_configs` list instead of three fixed per-tool structs (`source/flint.h`). `config_add_tool()` / `config_find_tool()` manage it.
  - Config file sections `[agy]`, `[claude]`, `[codex]` (aliases `[antigravity]`, `[openai]` accepted) set `wrapper`, `model` and `effort`.
  - `wrapper` is **empty by default** — no hardcoded `graperoot` launcher anymore, and the `--init-config` template documents that an empty `wrapper` means "run the raw CLI".
  - Testing: new tests in `tests/test_flint.c` (lookup, format with/without wrapper, config file parsing for `[agy]` and `[codex]` alias sections). `make test` → 17/17.
  - End-to-end: `flint --init-config` writes a working template; `flint -c <file> --summary --dry-run` resolves the matching tool and prepends the configured wrapper to the resume command.
  - Still open: a CLI override like `--wrapper <name>` isn't wired to `parse_args()`; the README has no `wrapper`/tool-config section yet.
- [x] **Verify the code is as performant as possible.** PARTIALLY DONE. No formal profiling or benchmarking has been run; rough timings noted earlier (0.14s / 0.04s). Done so far: `proc_is_session_running()` now caches the `ps -eo pid,command` output for the process lifetime instead of spawning `ps` per session.
  - Rough timing: `flint --summary --days 30` took about 0.14s and `--hours 1` about 0.04s, with only 1 session on this machine. That says little about a larger setup.
  - Likely hotspots, from reading the code. None are measured yet:
    - ~~`proc_is_session_running()` runs `ps -eo pid,command` once per session, plus an `lsof` per matching process.~~ DONE: ps output is now cached for the process lifetime; `lsof` per matching process remains for workspace checks.
    - ~~`git_inspect_workspace()` spawns 3 `git` subprocesses per session (`rev-parse`, `status`, `log`).~~ DONE: replaced `rev-parse` + `status` with a single `git status --porcelain -b`; only `git log` remains per session.
    - ~~`parse_transcript_line()` in `antigravity.c` mallocs and copies every line.~~ DONE: now reuses a scratch buffer in `agy_scan_ctx_t` instead of malloc/free per line.
    - ~~`proc_is_session_running()` runs `ps -eo pid,command` once per session, plus an `lsof` per matching process.~~ DONE: ps output cached for process lifetime; `lsof` cwd lookups now cached per pid (proc_lsof_cwd). `session_list_add()` dedupes with a linear scan. This is fine for small lists.
  - Profile with a realistic number of sessions before optimizing.
- [x] **Make the amount of hours to check for active sessions configurable.** DONE.
  - Config key `lookback_hours` (default 24), CLI `--hours <H>` and `-d/--days <N>`. The CLI overrides the config (`flint.c`, `main()`).
  - I ran it with a `lookback_hours` config file and the CLI flags. The sample data was inconclusive: the one transcript was 53 minutes old, which fits inside the 1-hour window. The override logic is straightforward when reading the code.
  - Small leftovers:
    - ~~`atoi()` silently ignores invalid input, so `--hours abc` is a no-op.~~ DONE: replaced with `parse_positive_int()` in `source/flint.c` for both `--days` and `--hours`.
    - ~~The README doesn't document the config key.~~ DONE: added `lookback_hours` to the Configuration table in the README.

## Other issues found while reviewing

- [x] `config_create_default_file()` opens the file with `"w"` and never checks whether it exists. Fixed: it now returns false when the target already exists, matching its header contract.
- [x] `--init-config` shells out to `mkdir -p ... 2>/dev/null`, which won't work on Windows. Fixed: added portable `fs_mkdir_p()` in `util/fs` and used it from `flint --init-config` and the journal `ensure_dir()`.
- [x] README is out of date. Addressed in the Scribe/MCP/brief work: added MCP/messaging section, `mcp_focus`/`--mcp`/`--no-mcp`, brief/focus sections, journal + configuration docs, and completed the usage block (`-j/--journal`, `--weekly`, `--monthly`, `--mcp`, `--no-mcp`, `-c`, `--init-config`). Removed the unimplemented `--origin` remnant from the usage text and README. Remaining: full config-file format reference — DONE this round (added the `### Full Configuration File Format` section with global keys and per-tool sections).
- [x] Antigravity falls back to `~` as the workspace when it can't find one. Fixed: when the transcript has no workspace, the session now falls back to the current working directory instead of the user's home directory. The `Active Workspaces:` / `Command Working Directory:` tag extraction itself still needs a real transcript to verify.
- [ ] Claude and Codex discovery looks like placeholder logic. Claude decodes `%2F` in directory names, and Codex treats top-level `~/.codex` entries as sessions with workspace `~`. I couldn't verify either against real data, since neither tool's data directory exists here. Check against real installs.
- [x] `proc_is_session_running()` matches `strstr(prog, tool_cmd)`, so a short name like `agy` can match unrelated command lines. Fixed: POSIX branch now extracts the executable basename from the first command token (after any path) and compares it to the tool name case-insensitively; workspace fallback (lsof cwd check) unchanged. Windows branch already compared exact exe names (`tool_cmd` vs `tool_cmd.exe`) and ignores the workspace — left as a known limitation.
- [x] Audit the codebase for remnants / cleanup after AI model & CLI switching experiments (stale flags, dead code, half-finished wiring, duplicated logic).
  - Audit findings (2026-11-02, post PR #1 merge):
    - Dead code: `flint_options_t.verbose` is declared (`source/flint.h`) but never read or used.
    - Doc-only remnant: `--origin <mode>` appeared in `print_usage` (`source/flint.c`) and the README usage block, but had no CLI parser handler or option field. **Resolved**: removed from both.
    - Half-wired remnant: `flint_config_t.open_in_tabs` is parsed, written to the config template, and documented, but never consumed by resume/launch logic.
    - No debug leftovers (DEBUG/FIXME/XXX/HACK) found in source.
    - No duplicated logic introduced by the merge resolution (fs_mkdir_p conflict resolved to single `int` version).
- [ ] Test coverage is thin. Nothing covers the AI adapters yet; `resume_mode` filtering now has unit tests via the extracted `resume_session_should_include()` helper (now covered, suite 35/32→35/35).
- [x] Housekeeping: untracked `flint.dSYM/` and the `flint` binary are ignored via `.gitignore`. Committed this TODO.md.