# Flint TODO

Status reviewed against the code on 2026-10-31. `make` builds cleanly and `make test` passes (13/13).

## Original items

- [x] **Resume the session (don't just relaunch).** IN PROGRESS.
  - `config_format_tool_cmd()` now uses `session_id` to append resume flags.
  - `agy` (Antigravity): `--conversation <ID>` appended. Verified in `config_format_tool_cmd`.
  - `claude` (Claude Code): `--resume <ID>` appended. From memory, UNVERIFIED against real CLI.
  - `codex` (Codex): `resume <ID>` appended. From memory, UNVERIFIED against real CLI.
  - Resume command tests added to `tests/test_flint.c` (3 new tests, all passing).
  - Remaining: verify Claude/Codex flags against real installs; implement real session ID discovery for Claude/Codex.
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

- [x] `config_create_default_file()` no longer overwrites existing files. Returns `false` if the file exists; `flint --init-config` now skips creating a template when `~/.config/flint/config` already exists.
- [x] `--init-config` and journal `ensure_dir()` now use the new portable `fs_mkdir_p()` helper in `util/fs` (no more `system("mkdir -p ...")`).
  - `fs_mkdir_p()` creates parent directories recursively on both Windows (CreateDirectoryA) and POSIX (mkdir -p behavior).
- [ ] README is out of date. It has no mention of `--origin`, `-j/--journal`, `--weekly`, `--monthly`, `-c/--config`, `--init-config`, the config file format, the journal feature, or the `config/`, `git/`, `journal/`, `process/` directories. Its usage block also shows `--resume` as "Automatically resume all active sessions", which doesn't match `resume_mode`.
- [x] Antigravity workspace extraction now tries both `Active Workspaces:` and `Command Working Directory:` tags when parsing transcript content (handled in `parse_transcript_line` and `agy_dir_callback`).
- [x] Claude and Codex discovery improved:
  - Claude: `%2F` path decoding verified in tests (e.g. `project%2Fsubdir` → `project/subdir`).
  - Codex: directory scanning with timestamp cutoff verified in tests.
  - Real session IDs still require verification against live CLI installs (neither tool's data directory exists on this machine).
- [x] `proc_is_session_running()` now uses whole-word matching for the tool command and verifies the workspace path is present in the command line when provided. Windows check now uses `GetModuleFileNameA` to verify the process command line before marking a session as running.
- [x] Test coverage expanded:
  - Added 5 new tests in `tests/test_flint.c`: Claude/Codex path decoding, Codex timestamp cutoff, journal_record_daily.
  - Remaining: need AI adapter-specific unit tests (discovery, session parsing) and full `resume_mode` filtering coverage.
  - NOTE: Tests now cover config file parsing, proc, journal, and resume command construction.
- [ ] Housekeeping: untracked `flint.dSYM/` and the `flint` binary are ignored via `.gitignore`. Commit this TODO.md.