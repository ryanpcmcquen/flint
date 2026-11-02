#ifndef FLINT_UTIL_EXEC_H
#define FLINT_UTIL_EXEC_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* Run a shell command and capture its stdout. Free the result with free(). */
char *exec_capture(const char *cmd);

/* Check whether a command is available on PATH. */
bool exec_command_exists(const char *name);

/* Quote a string so it can be embedded in a shell command. */
void exec_shell_quote(const char *in, char *out, size_t out_size);

#endif /* FLINT_UTIL_EXEC_H */
