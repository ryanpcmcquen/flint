#include "completion.h"
#include <stdio.h>
#include <string.h>

static const char BASH_COMPLETION[] =
    "# bash completion for flint\n"
    "_flint()\n"
    "{\n"
    "    local cur prev opts\n"
    "    COMPREPLY=()\n"
    "    cur=\"${COMP_WORDS[COMP_CWORD]}\"\n"
    "    prev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n"
    "\n"
    "    opts=\"-s --summary -r --resume --limit -t --terminal --wrapper -d --days --hours -w --workspace -j --journal --weekly --monthly --mcp --no-mcp -c --config --init-config -n --dry-run -l --list-terminals --json -v --version -h --help completion\"\n"
    "\n"
    "    case \"${prev}\" in\n"
    "        -t|--terminal)\n"
    "            COMPREPLY=( $(compgen -W \"apple-terminal iterm2 ghostty alacritty kitty gnome-terminal konsole windows-terminal powershell\" -- \"${cur}\") )\n"
    "            return 0\n"
    "            ;;\n"
    "        -c|--config)\n"
    "            COMPREPLY=( $(compgen -f -- \"${cur}\") )\n"
    "            return 0\n"
    "            ;;\n"
    "        -w|--workspace)\n"
    "            COMPREPLY=( $(compgen -d -- \"${cur}\") )\n"
    "            return 0\n"
    "            ;;\n"
    "        --limit|-d|--days|--hours)\n"
    "            return 0\n"
    "            ;;\n"
    "        completion)\n"
    "            COMPREPLY=( $(compgen -W \"bash zsh fish powershell\" -- \"${cur}\") )\n"
    "            return 0\n"
    "            ;;\n"
    "    esac\n"
    "\n"
    "    if [[ \"${cur}\" == -* ]]; then\n"
    "        COMPREPLY=( $(compgen -W \"${opts}\" -- \"${cur}\") )\n"
    "        return 0\n"
    "    fi\n"
    "\n"
    "    if [[ ${COMP_CWORD} -eq 1 ]]; then\n"
    "        COMPREPLY=( $(compgen -W \"completion\" -- \"${cur}\") )\n"
    "    fi\n"
    "}\n"
    "complete -F _flint flint\n";

static const char ZSH_COMPLETION[] =
    "#compdef flint\n"
    "\n"
    "_flint() {\n"
    "  local -a terminals shells\n"
    "  terminals=(apple-terminal iterm2 ghostty alacritty kitty gnome-terminal konsole windows-terminal powershell)\n"
    "  shells=(bash zsh fish powershell)\n"
    "\n"
    "  _arguments -s \\\n"
    "    '(-h --help)'{-h,--help}'[Print help]' \\\n"
    "    '(-v --version)'{-v,--version}'[Print version]' \\\n"
    "    '(-s --summary)'{-s,--summary}'[Display daily summary only]' \\\n"
    "    '(-r --resume)'{-r,--resume}'[Resume active sessions without prompting]' \\\n"
    "    '--limit[Resume only the N most recent sessions]:count:' \\\n"
    "    '(-t --terminal)'{-t,--terminal}'[Target terminal client]:terminal:($terminals)' \\\n"
    "    '--wrapper[Prepend a launcher to every resume command]:launcher:' \\\n"
    "    '(-d --days)'{-d,--days}'[Look back N days]:days:' \\\n"
    "    '--hours[Look back H hours]:hours:' \\\n"
    "    '(-w --workspace)'{-w,--workspace}'[Filter sessions to a workspace]:directory:_files -/' \\\n"
    "    '(-j --journal)'{-j,--journal}'[Record the daily standup log]' \\\n"
    "    '--weekly[Generate the weekly summary]' \\\n"
    "    '--monthly[Generate the monthly summary]' \\\n"
    "    '--mcp[Query connected MCP tools]' \\\n"
    "    '--no-mcp[Skip MCP queries]' \\\n"
    "    '(-c --config)'{-c,--config}'[Path to a custom config file]:file:_files' \\\n"
    "    '--init-config[Generate the default config template]' \\\n"
    "    '(-n --dry-run)'{-n,--dry-run}'[Show commands without launching]' \\\n"
    "    '(-l --list-terminals)'{-l,--list-terminals}'[List supported terminals]' \\\n"
    "    '--json[Output machine-readable JSON]' \\\n"
    "    '1:command:(completion)' \\\n"
    "    '2:shell:($shells)'\n"
    "}\n"
    "\n"
    "_flint \"$@\"\n";

static const char FISH_COMPLETION[] =
    "complete -c flint -f\n"
    "complete -c flint -s h -l help -d 'Print help'\n"
    "complete -c flint -s v -l version -d 'Print version'\n"
    "complete -c flint -s s -l summary -d 'Display daily summary only'\n"
    "complete -c flint -s r -l resume -d 'Resume active sessions without prompting'\n"
    "complete -c flint -l limit -x -d 'Resume only the N most recent sessions'\n"
    "complete -c flint -s t -l terminal -x -a 'apple-terminal iterm2 ghostty alacritty kitty gnome-terminal konsole windows-terminal powershell' -d 'Target terminal client'\n"
    "complete -c flint -l wrapper -x -d 'Prepend a launcher to every resume command'\n"
    "complete -c flint -s d -l days -x -d 'Look back N days'\n"
    "complete -c flint -l hours -x -d 'Look back H hours'\n"
    "complete -c flint -s w -l workspace -x -a '(__fish_complete_directories)' -d 'Filter sessions to a workspace'\n"
    "complete -c flint -s j -l journal -d 'Record the daily standup log'\n"
    "complete -c flint -l weekly -d 'Generate the weekly summary'\n"
    "complete -c flint -l monthly -d 'Generate the monthly summary'\n"
    "complete -c flint -l mcp -d 'Query connected MCP tools'\n"
    "complete -c flint -l no-mcp -d 'Skip MCP queries'\n"
    "complete -c flint -s c -l config -r -d 'Path to a custom config file'\n"
    "complete -c flint -l init-config -d 'Generate the default config template'\n"
    "complete -c flint -s n -l dry-run -d 'Show commands without launching'\n"
    "complete -c flint -s l -l list-terminals -d 'List supported terminals'\n"
    "complete -c flint -l json -d 'Output machine-readable JSON'\n"
    "complete -c flint -n '__fish_use_subcommand' -a completion -d 'Print shell completions'\n"
    "complete -c flint -n '__fish_seen_subcommand_from completion' -a 'bash zsh fish powershell'\n";

static const char POWERSHELL_COMPLETION[] =
    "Register-ArgumentCompleter -Native -CommandName flint -ScriptBlock {\n"
    "    param($wordToComplete, $commandAst, $cursorPosition)\n"
    "    $options = @('-s', '--summary', '-r', '--resume', '--limit', '-t', '--terminal',\n"
    "        '--wrapper', '-d', '--days', '--hours', '-w', '--workspace', '-j', '--journal',\n"
    "        '--weekly', '--monthly', '--mcp', '--no-mcp', '-c', '--config', '--init-config',\n"
    "        '-n', '--dry-run', '-l', '--list-terminals', '--json', '-v', '--version', '-h', '--help',\n"
    "        'completion')\n"
    "    $terminals = @('apple-terminal', 'iterm2', 'ghostty', 'alacritty', 'kitty',\n"
    "        'gnome-terminal', 'konsole', 'windows-terminal', 'powershell')\n"
    "    $shells = @('bash', 'zsh', 'fish', 'powershell')\n"
    "    $elements = @($commandAst.CommandElements | ForEach-Object { $_.Value })\n"
    "    $prev = if ($elements.Count -gt 1) { $elements[-1] } else { '' }\n"
    "\n"
    "    if ($prev -eq '-t' -or $prev -eq '--terminal') {\n"
    "        $terminals | Where-Object { $_ -like \"$wordToComplete*\" } | ForEach-Object {\n"
    "            [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)\n"
    "        }\n"
    "        return\n"
    "    }\n"
    "    if ($prev -eq 'completion') {\n"
    "        $shells | Where-Object { $_ -like \"$wordToComplete*\" } | ForEach-Object {\n"
    "            [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)\n"
    "        }\n"
    "        return\n"
    "    }\n"
    "    $options | Where-Object { $_ -like \"$wordToComplete*\" } | ForEach-Object {\n"
    "        [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterName', $_)\n"
    "    }\n"
    "}\n";

const char *completion_script(const char *shell) {
    if (!shell) return NULL;
    if (strcmp(shell, "bash") == 0) return BASH_COMPLETION;
    if (strcmp(shell, "zsh") == 0) return ZSH_COMPLETION;
    if (strcmp(shell, "fish") == 0) return FISH_COMPLETION;
    if (strcmp(shell, "powershell") == 0 || strcmp(shell, "pwsh") == 0) {
        return POWERSHELL_COMPLETION;
    }
    return NULL;
}

bool completion_print(const char *shell) {
    const char *script = completion_script(shell);
    if (!script) return false;
    fputs(script, stdout);
    return true;
}
