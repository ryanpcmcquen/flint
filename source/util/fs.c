#include "fs.h"
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#ifdef _WIN32
  #include <windows.h>
  #include <io.h>
  #include <direct.h>
  #define stat _stat
#else
  #include <unistd.h>
  #include <dirent.h>
  #include <pwd.h>
#endif

void fs_expand_path(const char *path, char *out, size_t out_size) {
    if (!path || !out || out_size == 0) return;

    if (path[0] == '~' && (path[1] == '/' || path[1] == '\\' || path[1] == '\0')) {
        const char *home = NULL;
#ifdef _WIN32
        home = getenv("USERPROFILE");
        if (!home) {
            home = getenv("HOME");
        }
#else
        home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            if (pw) home = pw->pw_dir;
        }
#endif
        if (home) {
            snprintf(out, out_size, "%s%s", home, path + 1);
            return;
        }
    }

    strncpy(out, path, out_size - 1);
    out[out_size - 1] = '\0';
}

bool fs_file_exists(const char *path) {
    if (!path) return false;
    char expanded[1024];
    fs_expand_path(path, expanded, sizeof(expanded));

    struct stat st;
    if (stat(expanded, &st) == 0) {
        return (st.st_mode & S_IFDIR) == 0;
    }
    return false;
}

int fs_mkdir_p(const char *path) {
    if (!path || !path[0]) return -1;

    char buf[1024];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

#ifdef _WIN32
    char *copy = (char *)malloc(strlen(buf) + 1);
    if (!copy) return -1;
    strcpy(copy, buf);
    for (char *p = copy + 1; *p; p++) {
        if (*p == PATH_SEP) {
            *p = '\0';
            _mkdir(copy);
            *p = PATH_SEP;
        }
    }
    _mkdir(buf);
    free(copy);
#else
    char *copy = (char *)malloc(strlen(buf) + 1);
    if (!copy) return -1;
    strcpy(copy, buf);
    for (char *p = copy + 1; *p; p++) {
        if (*p == PATH_SEP) {
            *p = '\0';
            mkdir(copy, 0777);
            *p = PATH_SEP;
        }
    }
    mkdir(copy, 0777);
    free(copy);
#endif
    return 0;
}

bool fs_dir_exists(const char *path) {
    if (!path) return false;
    char expanded[1024];
    fs_expand_path(path, expanded, sizeof(expanded));

    struct stat st;
    if (stat(expanded, &st) == 0) {
        return (st.st_mode & S_IFDIR) != 0;
    }
    return false;
}

void fs_path_join(const char *dir, const char *file, char *out, size_t out_size) {
    if (!dir || !file || !out || out_size == 0) return;

    size_t dlen = strlen(dir);
    if (dlen == 0) {
        strncpy(out, file, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }

    char last_char = dir[dlen - 1];
    if (last_char == '/' || last_char == '\\') {
        snprintf(out, out_size, "%s%s", dir, file);
    } else {
        snprintf(out, out_size, "%s" PATH_SEP_STR "%s", dir, file);
    }
}

char *fs_read_file(const char *path, size_t *out_size) {
    if (!path) return NULL;
    char expanded[1024];
    fs_expand_path(path, expanded, sizeof(expanded));

    FILE *f = fopen(expanded, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *buf = (char *)malloc(size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buf, 1, size, f);
    buf[read_bytes] = '\0';
    fclose(f);

    if (out_size) {
        *out_size = read_bytes;
    }
    return buf;
}

char *fs_read_tail(const char *path, size_t max_bytes, size_t *out_size) {
    if (!path) return NULL;
    char expanded[1024];
    fs_expand_path(path, expanded, sizeof(expanded));

    FILE *f = fopen(expanded, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size <= 0) {
        fclose(f);
        return NULL;
    }

    long start_offset = 0;
    size_t bytes_to_read = (size_t)size;
    if (bytes_to_read > max_bytes) {
        start_offset = size - (long)max_bytes;
        bytes_to_read = max_bytes;
    }

    fseek(f, start_offset, SEEK_SET);
    char *buf = (char *)malloc(bytes_to_read + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buf, 1, bytes_to_read, f);
    buf[read_bytes] = '\0';
    fclose(f);

    if (out_size) {
        *out_size = read_bytes;
    }
    return buf;
}

time_t fs_get_mod_time(const char *path) {
    if (!path) return 0;
    char expanded[1024];
    fs_expand_path(path, expanded, sizeof(expanded));

    struct stat st;
    if (stat(expanded, &st) == 0) {
        return st.st_mtime;
    }
    return 0;
}

void fs_format_time(time_t t, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (t <= 0) {
        strncpy(out, "Unknown", out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }

    struct tm *lt = localtime(&t);
    if (!lt) {
        strncpy(out, "Unknown", out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }

    strftime(out, out_size, "%Y-%m-%d %H:%M", lt);
}

int fs_list_dir(const char *dir_path, fs_dir_callback_t callback, void *userdata) {
    if (!dir_path || !callback) return -1;
    char expanded[1024];
    fs_expand_path(dir_path, expanded, sizeof(expanded));

#ifdef _WIN32
    char search_pattern[1024];
    fs_path_join(expanded, "*", search_pattern, sizeof(search_pattern));

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return -1;
    }

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
            continue;
        }
        bool is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        int res = callback(expanded, fd.cFileName, is_dir, userdata);
        if (res != 0) {
            FindClose(hFind);
            return res;
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
    return 0;
#else
    DIR *d = opendir(expanded);
    if (!d) return -1;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char full[1024];
        fs_path_join(expanded, entry->d_name, full, sizeof(full));

        struct stat st;
        bool is_dir = false;
        if (stat(full, &st) == 0) {
            is_dir = (st.st_mode & S_IFDIR) != 0;
        }

        int res = callback(expanded, entry->d_name, is_dir, userdata);
        if (res != 0) {
            closedir(d);
            return res;
        }
    }

    closedir(d);
    return 0;
#endif
}

void str_trim(char *str) {
    if (!str) return;
    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[len - 1] = '\0';
        len--;
    }
}

bool str_has_prefix(const char *str, const char *prefix) {
    if (!str || !prefix) return false;
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

bool str_has_suffix(const char *str, const char *suffix) {
    if (!str || !suffix) return false;
    size_t str_len = strlen(str);
    size_t suf_len = strlen(suffix);
    if (str_len < suf_len) return false;
    return strcmp(str + str_len - suf_len, suffix) == 0;
}

bool str_contains_ignore_case(const char *haystack, const char *needle) {
    if (!haystack || !needle) return false;
    if (!*needle) return true;

    size_t needle_len = strlen(needle);
    for (; *haystack; haystack++) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
            size_t i;
            for (i = 1; i < needle_len; i++) {
                if (tolower((unsigned char)haystack[i]) != tolower((unsigned char)needle[i])) {
                    break;
                }
            }
            if (i == needle_len) return true;
        }
    }
    return false;
}

void str_truncate_clean(char *dest, const char *src, size_t max_len) {
    if (!dest || !src || max_len == 0) return;
    size_t len = strlen(src);
    if (len <= max_len) {
        strncpy(dest, src, max_len);
        dest[max_len - 1] = '\0';
    } else {
        if (max_len > 3) {
            strncpy(dest, src, max_len - 4);
            dest[max_len - 4] = '\0';
            strcat(dest, "...");
        } else {
            strncpy(dest, src, max_len - 1);
            dest[max_len - 1] = '\0';
        }
    }
}
