#ifndef FLINT_AI_ADAPTER_H
#define FLINT_AI_ADAPTER_H

#include "../flint.h"

typedef struct ai_adapter {
    const char *name;
    const char *cli_binary;
    bool (*is_available)(void);
    int (*discover_sessions)(int lookback_hours, session_list_t *list);
} ai_adapter_t;

/* Available AI adapters */
extern const ai_adapter_t antigravity_adapter;
extern const ai_adapter_t claude_adapter;
extern const ai_adapter_t codex_adapter;

/* Master registry */
size_t ai_get_adapters(const ai_adapter_t ***adapters);

#endif /* FLINT_AI_ADAPTER_H */
