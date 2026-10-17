#ifndef LUNA_FILE_BROWSER_H
#define LUNA_FILE_BROWSER_H

#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <time.h>

// Includes entry capacity, visible indices, and filenames; per pane.
#define BROWSER_MEMORY_BUDGET (512U * 1024U)
#define BROWSER_FILTER_MAX 63
#define BROWSER_HISTORY_MAX 32

typedef struct {
  char *name;
  uint64_t size;
  time_t modified;
  int isDirectory;
  int hasSize;
  int metadataLoaded;
} BrowserEntry;

typedef enum {
  BROWSER_SORT_NAME, BROWSER_SORT_SIZE, BROWSER_SORT_DATE
} BrowserSort;

typedef struct BrowserLocation BrowserLocation;
typedef struct {
  BrowserEntry *entries;
  int *visible;
  int root, rootSelected, count, selected, truncated;
  int capacity, visibleCount, metadataNext;
  size_t nameBytes;
  char path[PATH_MAX + 1];
  char filter[BROWSER_FILTER_MAX + 1];
  char restoreName[256];
  int restoreSelected;
  BrowserSort sort;
  DIR *scanning;
  BrowserLocation *history;
  int historyCount;
} BrowserPane;

int browserNaturalCompare(const char *a, const char *b);
int browserNameMatches(const char *name, const char *filter);
BrowserEntry *browserPaneEntry(const BrowserPane *pane, int index);
int browserPaneOpen(BrowserPane *pane, const char *path);
int browserPaneEnter(BrowserPane *pane, const char *name);
int browserPaneUp(BrowserPane *pane, const char *root);
int browserPaneRefresh(BrowserPane *pane, const char *selectName);
void browserPaneStep(BrowserPane *pane);
void browserPaneFilter(BrowserPane *pane, const char *filter);
void browserPaneSort(BrowserPane *pane, BrowserSort sort);
void browserPaneStop(BrowserPane *pane);
void browserPaneClearHistory(BrowserPane *pane);
void browserPaneDestroy(BrowserPane *pane);

#endif
