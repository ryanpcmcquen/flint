#ifndef FLINT_JOURNAL_H
#define FLINT_JOURNAL_H

#include "../flint.h"

/* Record today's accomplishments and git status to a markdown file in the journal repo */
int journal_record_daily(const flint_config_t *cfg, const session_list_t *list);

/* Generate a weekly summary (past 7 days) and print/save it */
int journal_generate_weekly(const flint_config_t *cfg);

/* Generate a monthly summary (past 30 days) and print/save it */
int journal_generate_monthly(const flint_config_t *cfg);

#endif
