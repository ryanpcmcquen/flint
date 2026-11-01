#ifndef FLINT_GIT_H
#define FLINT_GIT_H

#include "../flint.h"

/* Check if a workspace is a Git repository */
bool git_is_repo(const char *workspace_path);

/* Inspect Git activity: branch, recent commits, uncommitted diffs, and suggested focus */
int git_inspect_workspace(const char *workspace_path, int lookback_hours, git_info_t *out_info);

#endif /* FLINT_GIT_H */
