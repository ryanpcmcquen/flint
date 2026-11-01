#ifndef FLINT_PROC_H
#define FLINT_PROC_H

#include "../flint.h"

/* Check if an AI CLI tool is already running in the given workspace */
bool proc_is_session_running(const char *tool_cmd, const char *workspace_path);

#endif /* FLINT_PROC_H */
