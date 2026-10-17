#include "ui/file_operations.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int deleteTree(const char *path, int depth,
                      BrowserDeleteProgress progress, void *context) {
  struct stat info;
  if (progress && progress(path, context)) return -2;
  if (depth > 16 || lstat(path, &info)) return -1;
  if (!S_ISDIR(info.st_mode)) return unlink(path) ? -1 : 0;
  DIR *directory = opendir(path);
  if (!directory) return -1;
  int result = 0;
  while (!result) {
    errno = 0;
    struct dirent *item = readdir(directory);
    if (!item) { if (errno) result = -1; break; }
    if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
    char *child = malloc(PATH_MAX + 1);
    if (!child) { result = -1; break; }
    int length = snprintf(child, PATH_MAX + 1, "%s/%s", path, item->d_name);
    result = length < 0 || length > PATH_MAX ? -1 :
        deleteTree(child, depth + 1, progress, context);
    free(child);
  }
  if (closedir(directory) && !result) result = -1;
  if (!result && progress && progress(path, context)) result = -2;
  if (!result && rmdir(path)) result = -1;
  return result;
}

int browserDeleteTree(const char *path, const char *root,
                      BrowserDeleteProgress progress, void *context) {
  size_t length = strlen(root);
  while (length && root[length - 1] == '/') length--;
  if (!length || strncmp(path, root, length) || path[length] != '/' ||
      !path[length + 1]) return -1;
  const char *component = path + length + 1;
  for (const char *p = component;; p++) {
    if (*p == ':' || *p == '\\') return -1;
    if (!*p || *p == '/') {
      size_t size = (size_t)(p - component);
      if (!size || (size == 1 && component[0] == '.') ||
          (size == 2 && component[0] == '.' && component[1] == '.')) return -1;
      if (!*p) break;
      component = p + 1;
    }
  }
  return deleteTree(path, 0, progress, context);
}
