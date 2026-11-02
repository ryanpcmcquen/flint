#ifndef FLINT_BRIEF_H
#define FLINT_BRIEF_H

#include "../flint.h"

#define BRIEF_MAX_FOCUS 5

typedef struct {
    char text[MAX_SUMMARY_LEN];
    char workspace[MAX_PATH_LEN];
} focus_item_t;

typedef struct {
    focus_item_t focus[BRIEF_MAX_FOCUS];
    size_t focus_count;
} brief_t;

/* Work out the most important things to pick up today, one per workspace. */
void brief_build(const session_list_t *list, brief_t *out);

/* Print a presented, grouped overview of recent work. */
void brief_print_overview(const session_list_t *list, int lookback_hours);

/* Print the ranked "Focus for today" section. */
void brief_print_focus(const brief_t *brief);

/* Format focus items as a plain bullet list for an AI prompt. */
void brief_focus_context(const brief_t *brief, char *out, size_t out_size);

/* Add focus bullets returned by connected MCP tools. Returns true if any
 * new items were added. */
bool brief_add_mcp_focus(brief_t *brief, const char *text, size_t max_items,
                        const char *workspace);

#endif /* FLINT_BRIEF_H */
