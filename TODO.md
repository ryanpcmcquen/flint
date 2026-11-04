# Flint TODO

Status reviewed against the code on 2026-11-04. `make` builds cleanly and `make test` passes (60/60).

## Original items

- [ ] **Resume the session (don't just relaunch).** IN PROGRESS. Resume command wiring completed this round: `agy` emits ` --conversation <ID>`, `claude` emits ` --resume <ID>`, and `codex` emits ` resume <ID>` when a session id is present (tests added; `make test` 28/28). PR #1 conflicts resolved via merge of `origin/main` (merge commit `8a0e677`); resume end-to-end still blocked on real CLI installs / session ids.
  - `config_format_tool_cmd()` in `source/config/config.c` takes `session_id` but the `agy` branch ignored it. Fixed: the `agy` branch now emits ` --conversation <session_id>` when a session id is present, so the resume command becomes `[wrapper] agy --conversation <ID> --model ... --effort ...`. The `claude` branch now emits ` --resume <session_id>` and the `codex` branch emits ` resume <session_id>` when a session id is present.
  - `agy` has `--conversation <ID>` ("Resume a previous conversation by ID") and `--continue`. I confirmed this with `agy --help`. Antigravity's `session.id` is the brain directory name, so it should be usable as the ID. Not yet tested against a real resume.
  - Claude and Codex: neither CLI is installed on this machine, so I couldn't check their flags. Look up the real resume syntax before wiring them up. From memory, and unverified, Claude has `--resume`/`--continue` and Codex has a `resume` subcommand. Resume commands were wired anyway (`claude --resume <id>`, `codex resume <id>`) with tests added; flags still need real-CLI verification. **Docs verified (2026-11-04):** Claude's `--resume <session-id>` and Codex's `codex resume <id>` are the documented forms; discovery now produces real IDs.
  - Claude and Codex session IDs aren't real session IDs yet. `claude.c` uses the project directory name or the placeholder `"claude-session"`. `codex.c` uses the top-level entry name in `~/.codex`. Both need real discovery of session files (e.g. the `.jsonl` file names) before an ID-based resume can work. **FIXED (2026-11-04):** real transcript/rollout discovery now supplies the actual session/thread IDs.
  - Add a test for the resume command string. DONE: added `config_format_tool_cmd resumes Antigravity sessions via --conversation`, `... resumes OpenCode sessions`, `... resumes Claude sessions via --resume`, and `... resumes Codex sessions via resume subcommand` tests in `tests/test_flint.c`; `make test` now passes 28/28. End-to-end resume still needs real CLI installs.
  - **RESUME E2E DONE THIS ROUND (2026-11-04):** real CLIs now installed & exercised — `agy`, `cline`, `opencode`, and newly-discovered `openclaw`. `flint -r -n` (resume + dry-run) now walks the full pipeline: discovery → `resume_mode` filter → not-running check → `config_format_tool_cmd` → terminal launch dry-run. All installed tools produce correct resume commands:
    - `agy --conversation <ID> --model gemini-3.8-flash --effort high`
    - `cline --id <ID>`
    - `opencode --session <ID>`
    - `openclaw resume agent:main:main` (new OpenClaw adapter: discovery via `openclaw sessions --json`, resume via `openclaw resume <key>`)
    - `claude` / `codex`: still missing installs (flags unverified).
  - Added OpenClaw adapter (`source/ai/openclaw.c`, registered in the adapter registry) + `config_format_tool_cmd resumes OpenClaw sessions via resume` test; suite now **39/39** (40/40 after the OpenClaw discovery test).
- [x] **Make the inclusion of GrapeRoot configurable.** DONE.
  - Generalized into a full tool list: `flint_config_t` now holds an ordered `tool_configs` list instead of three fixed per-tool structs (`source/flint.h`). `config_add_tool()` / `config_find_tool()` manage it.
  - Config file sections `[agy]`, `[claude]`, `[codex]` (aliases `[antigravity]`, `[openai]` accepted) set `wrapper`, `model` and `effort`.
  - `wrapper` is **empty by default** — no hardcoded `graperoot` launcher anymore, and the `--init-config` template documents that an empty `wrapper` means "run the raw CLI".
  - Testing: new tests in `tests/test_flint.c` (lookup, format with/without wrapper, config file parsing for `[agy]` and `[codex]` alias sections). `make test` → 17/17.
  - End-to-end: `flint --init-config` writes a working template; `flint -c <file> --summary --dry-run` resolves the matching tool and prepends the configured wrapper to the resume command.
  - `--wrapper <name>` is now wired: it applies the launcher to every configured tool at runtime (`config_set_wrapper_all()`), covering tools without a config-file `wrapper` entry. README documents both the CLI flag and the tool-config format.
- [x] **Verify the code is as performant as possible.** DONE for all identified hotspots; formal profiling/benchmarking still open.
  - Done so far: `proc_is_session_running()` caches the `ps -eo pid,command` output for the process lifetime instead of spawning `ps` per session.
  - Done so far: `lsof` cwd lookups are now cached per pid (`proc_lsof_cwd`).
  - Done so far: `git_inspect_workspace()` uses a single `git status --porcelain -b` (was 3 git subprocesses: `rev-parse`, `status`, `log`).
  - Done so far: `parse_transcript_line()` reuses a scratch buffer in `agy_scan_ctx_t` instead of malloc/free per line.
  - **BUG FOUND & FIXED (2026-11-03):** the scratch-buffer refactor left a stale `free(buf)` behind, so the callback freed a stack buffer (`agy_scan_ctx_t.line_buf`) → glibc abort: `malloc: *** error for object ...: pointer being freed was not allocated` → SIGABRT on `./flint --summary`. **Fixed**: removed the stale `free`; added regression tests feeding real transcript lines through the parser (`make test` → 40/40).
  - Measurements (2026-11-04, macOS arm64): `./flint --summary` ≈ **3.4s**; `./flint -r -n` ≈ **1.2s**; `./flint --summary --days 7` ≈ **1.5s**, with 4 AI sessions across 3 workspaces.
  - Formal profiling/benchmarking: tooling unavailable in this environment (no gdb/valgrind/perf; dtrace exec tracing restricted by SIP) — code-path reasoning + wall-clock timings used instead.
  - Profile with a realistic number of sessions before optimizing further. `session_list_add()` dedupes with a linear scan. This is fine for small lists.
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
- [x] Claude and Codex discovery looks like placeholder logic. **FIXED (2026-11-04):** both adapters now parse the real on-disk stores, verified against the official docs and Codex's rollout source, and covered by fixture-based parser + discovery tests (no CLI binaries required).
  - Claude: recursively scans `~/.claude/projects/**/*.jsonl`, reads the transcript head for `sessionId`, `cwd`, `isSidechain`, custom titles, generated summaries, and the first real user prompt. Resume: `claude --resume <session-id>`.
  - Codex: recursively scans `$CODEX_HOME/sessions/YYYY/MM/DD/rollout-*.jsonl` (default `~/.codex`), parses the `session_meta` payload for `id`/`cwd`/git branch/subagent metadata, and falls back to the first bare metadata object used by older rollouts. Summaries come from `event_msg`/`response_item` user messages. Resume: `codex resume <thread-id>`.
  - The old placeholder logic (Claude `"claude-session"` IDs and `%2F` project-name decoding; Codex top-level `~/.codex` entries with workspace `~`) is gone.
  - Fixture tests cover array/string Claude content, custom titles, sidechains, Codex `session_meta`, `event_msg`, `response_item`, subagent metadata, malformed input, and temp-home discovery. E2E dry-run against a synthetic `HOME` produced both resume commands. Suite is now **60/60**.
  - Known limits: Codex rollouts compressed as `.jsonl.zst` and sessions under `~/.codex/archived_sessions/` are not scanned yet; Claude's undocumented transcript schema can change between releases (the parser tolerates unknown lines and truncated tails).
- [x] `proc_is_session_running()` matches `strstr(prog, tool_cmd)`, so a short name like `agy` can match unrelated command lines. Fixed: POSIX branch now extracts the executable basename from the first command token (after any path) and compares it to the tool name case-insensitively; workspace fallback (lsof cwd check) unchanged. Windows branch already compared exact exe names (`tool_cmd` vs `tool_cmd.exe`) and ignores the workspace — left as a known limitation.
- [x] Audit the codebase for remnants / cleanup after AI model & CLI switching experiments (stale flags, dead code, half-finished wiring, duplicated logic).
  - Audit findings (2026-11-02, post PR #1 merge):
    - Dead code: `flint_options_t.verbose` is declared (`source/flint.h`) but never read or used.
    - Doc-only remnant: `--origin <mode>` appeared in `print_usage` (`source/flint.c`) and the README usage block, but had no CLI parser handler or option field. **Resolved**: removed from both.
    - Half-wired remnant: `flint_config_t.open_in_tabs` is parsed, written to the config template, and documented, but never consumed by resume/launch logic. **FIXED:** `open_in_tabs` is now passed through `resume_sessions()` into every terminal adapter, which opens tabs or windows accordingly (Apple Terminal, iTerm2, Ghostty, Kitty, GNOME Terminal, Konsole, Windows Terminal; Alacritty and PowerShell have no tab API and always open windows).
    - No debug leftovers (DEBUG/FIXME/XXX/HACK) found in source.
    - No duplicated logic introduced by the merge resolution (fs_mkdir_p conflict resolved to single `int` version).
- [ ] Test coverage is thin. AI adapter internals (Antigravity transcript parsing via `flint_agy_parse_line` regression tests; OpenClaw discovery via adapter-level test; Claude/Codex head parsing + temp-home discovery; MCP focus/query + `mcp_collect_line` internals via new tests) are covered — suite now **60/60**. Config defaults now cover OpenClaw and `open_in_tabs`, plus the `--wrapper` override and `[openclaw]` section parsing. `proc` and `resume_mode` filtering are covered. Remaining gap: real-CLI Claude/Codex resume e2e still needs the binaries installed.
- [x] Housekeeping: untracked `flint.dSYM/` and the `flint` binary are ignored via `.gitignore`. Committed this TODO.md.
- [x] OpenClaw was discovered/resumed but missing from the default tool list, `--init-config` template, and README config example. Fixed: `config_init_defaults()` now adds `openclaw`, the template includes an `[openclaw]` section, and the README mirrors it.
- [x] **Heap-buffer-overflow in `run_prompt()` (`source/ai/mcp.c`), found with ASan:** the command buffer size was computed from `strlen()` of freshly malloc'd, uninitialized quote buffers. Fixed by quoting first and sizing the command from the initialized strings; the MCP prompt test no longer invokes `opencode` for a rejected input. Full suite is ASan/UBSan-clean.
