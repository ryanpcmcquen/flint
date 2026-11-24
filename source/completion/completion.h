#ifndef FLINT_COMPLETION_H
#define FLINT_COMPLETION_H

#include <stdbool.h>

/* Return a static completion script for the requested shell, or NULL. */
const char *completion_script(const char *shell);

/* Print the completion script. Returns false for an unknown shell. */
bool completion_print(const char *shell);

#endif
