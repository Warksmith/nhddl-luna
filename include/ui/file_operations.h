#ifndef LUNA_FILE_OPERATIONS_H
#define LUNA_FILE_OPERATIONS_H
typedef int (*BrowserDeleteProgress)(const char *path, void *context);
// Only strict descendants of root are removable. -2 means user cancellation.
int browserDeleteTree(const char *path, const char *root,
                      BrowserDeleteProgress progress, void *context);
#endif
