#ifndef FLINT_UTIL_FS_H
#define FLINT_UTIL_FS_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

#ifdef _WIN32
  #define PATH_SEP '\\'
  #define PATH_SEP_STR "\\"
#else
  #define PATH_SEP '/'
  #define PATH_SEP_STR "/"
#endif

/* Expand paths beginning with '~' or relative to user home */
void fs_expand_path(const char *path, char *out, size_t out_size);

/* Check if file or directory exists */
bool fs_file_exists(const char *path);
bool fs_dir_exists(const char *path);

/* Safe cross-platform path join */
void fs_path_join(const char *dir, const char *file, char *out, size_t out_size);

/* Read entire file into null-terminated allocated buffer. Free with free(). */
char *fs_read_file(const char *path, size_t *out_size);

/* Read up to max_bytes from the tail of a file. Free with free(). */
char *fs_read_tail(const char *path, size_t max_bytes, size_t *out_size);

/* Get last modification time of a file/directory */
time_t fs_get_mod_time(const char *path);

/* Format timestamp into readable local time "YYYY-MM-DD HH:MM" */
void fs_format_time(time_t t, char *out, size_t out_size);

/* Callback type for directory listing. Return 0 to continue, non-zero to stop. */
typedef int (*fs_dir_callback_t)(const char *parent, const char *entry_name, bool is_dir, void *userdata);

/* List contents of a directory cross-platform */
int fs_list_dir(const char *dir_path, fs_dir_callback_t callback, void *userdata);

/* String utilities */
void str_trim(char *str);
bool str_has_prefix(const char *str, const char *prefix);
bool str_has_suffix(const char *str, const char *suffix);
bool str_contains_ignore_case(const char *haystack, const char *needle);
void str_truncate_clean(char *dest, const char *src, size_t max_len);

#endif /* FLINT_UTIL_FS_H */
