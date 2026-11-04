#include "brief.h"
#include "../util/exec.h"
#include "../util/fs.h"
#include <string.h>
#include <ctype.h>

static void workspace_label(const char *path, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (!path || path[0] == '\0') {
        snprintf(out, out_size, "unknown workspace");
        return;
    }

    const char *last = strrchr(path, '/');
#ifdef _WIN32
    const char *back = strrchr(path, '\\');
    if (back && (!last || back > last)) last = back;
#endif

    const char *base = last ? last + 1 : path;
    if (base[0] == '\0') base = path;
    snprintf(out, out_size, "%s", base);
}

/* Skip summaries that read like account questions or one-off checks. */
static bool summary_is_actionable(const char *summary) {
    if (!summary || summary[0] == '\0') return false;

    char lower[MAX_SUMMARY_LEN];
    size_t i = 0;
    for (; summary[i] && i + 1 < sizeof(lower); i++) {
        lower[i] = (char)tolower((unsigned char)summary[i]);
    }
    lower[i] = '\0';

    static const char *informational_starts[] = {
        "do ", "does ", "did ", "can ", "could ", "should ", "would ",
        "what ", "why ", "how ", "is ", "are ", "am ", "will ", "hello",
        "hi ", "hey "
    };
    for (size_t p = 0; p < sizeof(informational_starts) / sizeof(informational_starts[0]); p++) {
        if (strncmp(lower, informational_starts[p], strlen(informational_starts[p])) == 0) {
            return false;
        }
    }

    static const char *informational_terms[] = {
        "credit", "quota", "rate limit", "usage limit", "are you there"
    };
    for (size_t p = 0; p < sizeof(informational_terms) / sizeof(informational_terms[0]); p++) {
        if (strstr(lower, informational_terms[p])) {
            return false;
        }
    }

    return true;
}

/* Return session indices ordered from most recent to oldest. */
static size_t *sorted_sessions(const session_list_t *list) {
    if (!list || list->count == 0) return NULL;

    size_t *order = (size_t *)malloc(list->count * sizeof(size_t));
    if (!order) return NULL;

    for (size_t i = 0; i < list->count; i++) {
        order[i] = i;
    }

    for (size_t i = 1; i < list->count; i++) {
        size_t key = order[i];
        size_t j = i;
        while (j > 0 &&
               list->items[order[j - 1]].last_active < list->items[key].last_active) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = key;
    }

    return order;
}

void brief_build(const session_list_t *list, brief_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!list || list->count == 0) return;

    size_t *order = sorted_sessions(list);
    if (!order) return;

    bool *seen = (bool *)calloc(list->count, sizeof(bool));
    if (!seen) {
        free(order);
        return;
    }

    for (size_t oi = 0; oi < list->count && out->focus_count < BRIEF_MAX_FOCUS; oi++) {
        size_t idx = order[oi];
        if (seen[idx]) continue;

        const session_t *s = &list->items[idx];

        /* One focus item per workspace. */
        for (size_t j = 0; j < list->count; j++) {
            if (strcmp(list->items[j].workspace, s->workspace) == 0) {
                seen[j] = true;
            }
        }

        const char *text = NULL;

        /* 1. Uncommitted changes are the clearest sign of unfinished work. */
        for (size_t j = 0; j < list->count && !text; j++) {
            if (strcmp(list->items[j].workspace, s->workspace) != 0) continue;
            const git_info_t *g = &list->items[j].git;
            if (g->is_git_repo && g->uncommitted_count > 0) {
                text = g->suggested_focus[0] != '\0'
                     ? g->suggested_focus
                     : "Finish uncommitted changes";
            }
        }

        /* 2. Otherwise continue from the most recent commit. */
        for (size_t j = 0; j < list->count && !text; j++) {
            if (strcmp(list->items[j].workspace, s->workspace) != 0) continue;
            const git_info_t *g = &list->items[j].git;
            if (g->is_git_repo && g->commit_count > 0 && g->suggested_focus[0] != '\0') {
                text = g->suggested_focus;
            }
        }

        /* 3. Otherwise pick up the latest meaningful session summary. */
        char pick[MAX_SUMMARY_LEN];
        pick[0] = '\0';
        if (!text) {
            time_t pick_time = 0;
            for (size_t j = 0; j < list->count; j++) {
                const session_t *candidate = &list->items[j];
                if (strcmp(candidate->workspace, s->workspace) != 0) continue;
                if (candidate->was_merely_open || candidate->summary[0] == '\0') continue;
                if (!summary_is_actionable(candidate->summary)) continue;
                if (candidate->last_active >= pick_time) {
                    pick_time = candidate->last_active;
                    snprintf(pick, sizeof(pick), "Pick up: %s", candidate->summary);
                }
            }
            if (pick[0] != '\0') {
                text = pick;
            }
        }

        /* Nothing actionable in this workspace. */
        if (text == NULL) continue;

        focus_item_t *item = &out->focus[out->focus_count];
        strncpy(item->workspace, s->workspace, sizeof(item->workspace) - 1);
        str_truncate_clean(item->text, text, sizeof(item->text) - 1);
        out->focus_count++;
    }

    free(seen);
    free(order);
}

void brief_print_overview(const session_list_t *list, int lookback_hours) {
    if (!list || list->count == 0) return;

    size_t *order = sorted_sessions(list);
    if (!order) return;

    bool *printed = (bool *)calloc(list->count, sizeof(bool));
    if (!printed) {
        free(order);
        return;
    }

    size_t workspace_count = 0;
    char tool_names[16][MAX_NAME_LEN];
    size_t tool_count = 0;

    for (size_t i = 0; i < list->count; i++) {
        bool workspace_seen = false;
        for (size_t j = 0; j < i; j++) {
            if (strcmp(list->items[j].workspace, list->items[i].workspace) == 0) {
                workspace_seen = true;
                break;
            }
        }
        if (!workspace_seen) workspace_count++;

        if (list->items[i].tool_name[0] == '\0' || tool_count >= 16) continue;
        bool tool_seen = false;
        for (size_t t = 0; t < tool_count; t++) {
            if (strcmp(tool_names[t], list->items[i].tool_name) == 0) {
                tool_seen = true;
                break;
            }
        }
        if (!tool_seen) {
            strncpy(tool_names[tool_count], list->items[i].tool_name, MAX_NAME_LEN - 1);
            tool_names[tool_count][MAX_NAME_LEN - 1] = '\0';
            tool_count++;
        }
    }

    printf(ANSI_BOLD "  Morning Brief\n" ANSI_RESET);
    printf("  " ANSI_DIM "────────────────────────────────────────────────────────────" ANSI_RESET "\n");

    if (lookback_hours > 0) {
        printf("  In the last %d hours you worked in %zu workspace%s across %zu AI session%s",
               lookback_hours, workspace_count, workspace_count == 1 ? "" : "s",
               list->count, list->count == 1 ? "" : "s");
    } else {
        printf("  Recently you worked in %zu workspace%s across %zu AI session%s",
               workspace_count, workspace_count == 1 ? "" : "s",
               list->count, list->count == 1 ? "" : "s");
    }

    if (tool_count > 0) {
        printf(" using ");
        for (size_t i = 0; i < tool_count; i++) {
            printf("%s%s", i == 0 ? "" : ", ", tool_names[i]);
        }
    }
    printf(".\n\n");

    for (size_t oi = 0; oi < list->count; oi++) {
        size_t idx = order[oi];
        if (printed[idx]) continue;

        const session_t *s = &list->items[idx];
        const char *ws = s->workspace;

        char label[MAX_PATH_LEN];
        workspace_label(ws, label, sizeof(label));

        printf("  " ANSI_BOLD ANSI_CYAN "%s" ANSI_RESET, label);
        if (strcmp(label, ws) != 0 && ws[0] != '\0') {
            printf(" " ANSI_DIM "%s" ANSI_RESET, ws);
        }
        printf("\n");

        size_t shown = 0;
        for (size_t oj = 0; oj < list->count; oj++) {
            size_t j = order[oj];
            if (strcmp(list->items[j].workspace, ws) != 0) continue;
            printed[j] = true;

            if (shown >= 4) {
                shown++;
                continue;
            }

            const session_t *item = &list->items[j];
            char summary[192];
            summary[0] = '\0';
            if (item->summary[0] != '\0') {
                str_truncate_clean(summary, item->summary, sizeof(summary) - 1);
            } else {
                snprintf(summary, sizeof(summary), "Recent session");
            }

            printf("    %s%s%s · %s",
                   item->was_merely_open ? ANSI_YELLOW : ANSI_GREEN,
                   item->was_merely_open ? "⚡ " : "",
                   item->tool_name, summary);

            size_t time_len = strlen(item->last_active_str);
            if (time_len >= 5) {
                printf("  " ANSI_DIM "%s" ANSI_RESET, item->last_active_str + time_len - 5);
            }
            printf("\n");
            shown++;
        }

        if (shown > 4) {
            printf("    " ANSI_DIM "… and %zu more session(s)" ANSI_RESET "\n", shown - 4);
        }

        for (size_t oj = 0; oj < list->count; oj++) {
            size_t j = order[oj];
            if (strcmp(list->items[j].workspace, ws) != 0) continue;
            const git_info_t *g = &list->items[j].git;
            if (!g->is_git_repo) continue;

            printf("    " ANSI_DIM "Git:" ANSI_RESET " %s · %s\n",
                   g->branch[0] != '\0' ? g->branch : "HEAD",
                   g->uncommitted_summary[0] != '\0' ? g->uncommitted_summary : "working tree");
            break;
        }

        printf("\n");
    }

    free(printed);
    free(order);
}

void brief_print_focus(const brief_t *brief) {
    printf(ANSI_BOLD "  Focus for today\n" ANSI_RESET);
    printf("  " ANSI_DIM "────────────────────────────────────────────────────────────" ANSI_RESET "\n");

    if (!brief || brief->focus_count == 0) {
        printf("    Review your active workspaces and pick up where you left off.\n\n");
        return;
    }

    for (size_t i = 0; i < brief->focus_count; i++) {
        const focus_item_t *item = &brief->focus[i];
        char label[MAX_PATH_LEN];
        workspace_label(item->workspace, label, sizeof(label));

        printf("    " ANSI_YELLOW "%zu." ANSI_RESET " " ANSI_BOLD "%s" ANSI_RESET " — %s\n",
               i + 1, label, item->text);
    }
    printf("\n");
}

void brief_focus_context(const brief_t *brief, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!brief) return;

    size_t used = 0;
    for (size_t i = 0; i < brief->focus_count; i++) {
        char label[MAX_PATH_LEN];
        workspace_label(brief->focus[i].workspace, label, sizeof(label));

        char line[MAX_SUMMARY_LEN + MAX_PATH_LEN + 16];
        snprintf(line, sizeof(line), "- %s: %s\n", label, brief->focus[i].text);

        size_t len = strlen(line);
        if (used + len + 1 >= out_size) break;
        memcpy(out + used, line, len);
        used += len;
        out[used] = '\0';
    }
}

bool brief_add_mcp_focus(brief_t *brief, const char *text, size_t max_items,
                        const char *workspace) {
    if (!brief || !text) return false;

    bool added = false;
    const char *p = text;
    while (*p) {
        while (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        const char *start = p;
        while (*p && *p != '\n' && *p != '\r') p++;

        char line[MAX_SUMMARY_LEN];
        size_t len = (size_t)(p - start);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, start, len);
        line[len] = '\0';
        str_trim(line);

        if (line[0] == '\0') continue;

        if (line[0] == '-' && line[1] == ' ') {
            char bullet[MAX_SUMMARY_LEN];
            snprintf(bullet, sizeof(bullet), "%s", line + 2);
            str_trim(bullet);

            bool duplicate = false;
            for (size_t i = 0; i < brief->focus_count; i++) {
                if (workspace && strcmp(brief->focus[i].text, bullet) == 0) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate && brief->focus_count < max_items) {
                strncpy(brief->focus[brief->focus_count].workspace, workspace, sizeof(brief->focus[brief->focus_count].workspace) - 1);
                brief->focus[brief->focus_count].workspace[sizeof(brief->focus[0].workspace) - 1] = '\0';
                str_truncate_clean(brief->focus[brief->focus_count].text, bullet,
                                   sizeof(brief->focus[brief->focus_count].text) - 1);
                brief->focus_count++;
                added = true;
            }
        }
    }

    return added;
}

bool brief_add_scribe_focus(brief_t *brief, const char *text, size_t max_items) {
    if (!brief || !text) return false;

    bool added = false;
    const char *p = text;
    while (*p && brief->focus_count < max_items) {
        while (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        const char *start = p;
        while (*p && *p != '\n' && *p != '\r') p++;

        char line[MAX_SUMMARY_LEN];
        size_t len = (size_t)(p - start);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, start, len);
        line[len] = '\0';
        str_trim(line);

        if (line[0] != '-' || line[1] != ' ') continue;

        char *body = line + 2;
        str_trim(body);
        if (body[0] == '\0') continue;

        char ws[MAX_PATH_LEN] = "scribe";
        char summary[MAX_SUMMARY_LEN] = {0};

        char *colon = strstr(body, ": ");
        if (colon && colon > body && (size_t)(colon - body) < sizeof(ws)) {
            size_t ws_len = (size_t)(colon - body);
            memcpy(ws, body, ws_len);
            ws[ws_len] = '\0';
            str_trim(ws);
            snprintf(summary, sizeof(summary), "%s", colon + 2);
            str_trim(summary);
        } else {
            snprintf(summary, sizeof(summary), "%s", body);
        }

        if (summary[0] == '\0') continue;

        bool duplicate = false;
        for (size_t i = 0; i < brief->focus_count; i++) {
            char existing_label[MAX_PATH_LEN];
            workspace_label(brief->focus[i].workspace, existing_label, sizeof(existing_label));
            if (strcasecmp(brief->focus[i].text, summary) == 0) {
                duplicate = true;
                break;
            }
            if (strcasecmp(ws, "reminder") != 0 &&
                strcasecmp(existing_label, ws) == 0) {
                duplicate = true;
                break;
            }
        }

        if (!duplicate) {
            focus_item_t *item = &brief->focus[brief->focus_count];
            strncpy(item->workspace, ws, sizeof(item->workspace) - 1);
            item->workspace[sizeof(item->workspace) - 1] = '\0';
            str_truncate_clean(item->text, summary, sizeof(item->text) - 1);
            brief->focus_count++;
            added = true;
        }
    }

    return added;
}

bool brief_collect_scribe_focus(brief_t *brief, size_t max_items) {
    if (!brief || brief->focus_count >= max_items) return false;
    if (!exec_command_exists("scribe")) return false;

    char *out = exec_capture("scribe focus --bullets --limit 5 2>/dev/null");
    if (!out) return false;

    bool added = brief_add_scribe_focus(brief, out, max_items);
    free(out);
    return added;
}
