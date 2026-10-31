#ifndef FLINT_UTIL_JSON_H
#define FLINT_UTIL_JSON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* Unescape a JSON string literal into plain text */
void json_unescape(const char *src, char *dst, size_t dst_size);

/* Find string value for key in JSON object: "key": "value" */
bool json_get_string(const char *json, const char *key, char *out, size_t out_size);

/* Find integer value for key in JSON object: "key": 123 */
bool json_get_int(const char *json, const char *key, long *out);

/* Find boolean value for key: "key": true / false */
bool json_get_bool(const char *json, const char *key, bool *out);

/* Extract an inner JSON block (object {} or array []) */
bool json_get_block(const char *json, const char *key, char *out, size_t out_size);

/* Callback for processing each line in a JSONL buffer */
typedef int (*jsonl_line_cb_t)(const char *line, size_t len, size_t line_number, void *userdata);

/* Iterate over each line in a JSONL buffer */
int jsonl_for_each_line(const char *content, jsonl_line_cb_t callback, void *userdata);

/* Extract clean text from markdown/XML tags like <USER_REQUEST>...</USER_REQUEST> */
void extract_tagged_text(const char *src, const char *tag, char *dst, size_t dst_size);

#endif /* FLINT_UTIL_JSON_H */
