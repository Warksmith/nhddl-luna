#include "ui/file_browser.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct BrowserLocation {
  struct BrowserLocation *previous;
  char *path, *name;
  char filter[BROWSER_FILTER_MAX + 1];
  int selected;
};

int browserNaturalCompare(const char *a, const char *b) {
  const char *originalA = a, *originalB = b;
  while (*a && *b) {
    if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
      const char *endA, *endB;
      while (*a == '0') a++;
      while (*b == '0') b++;
      for (endA = a; isdigit((unsigned char)*endA); endA++);
      for (endB = b; isdigit((unsigned char)*endB); endB++);
      if (endA - a != endB - b) return endA - a < endB - b ? -1 : 1;
      int order = strncmp(a, b, (size_t)(endA - a));
      if (order) return order;
      a = endA;
      b = endB;
    } else {
      int order = tolower((unsigned char)*a) - tolower((unsigned char)*b);
      if (order) return order;
      a++;
      b++;
    }
  }
  if (*a || *b) return *a ? 1 : -1;
  return strcmp(originalA, originalB);
}

int browserNameMatches(const char *name, const char *filter) {
  if (!*filter) return 1;
  for (; *name; name++) {
    const char *a = name, *b = filter;
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
      a++;
      b++;
    }
    if (!*b) return 1;
  }
  return 0;
}

BrowserEntry *browserPaneEntry(const BrowserPane *pane, int index) {
  return index >= 0 && index < pane->visibleCount ?
      &pane->entries[pane->visible[index]] : NULL;
}

static void clearListing(BrowserPane *pane) {
  for (int i = 0; i < pane->count; i++) free(pane->entries[i].name);
  free(pane->entries);
  free(pane->visible);
  pane->entries = NULL;
  pane->visible = NULL;
  pane->count = pane->capacity = pane->visibleCount = 0;
  pane->nameBytes = 0;
}

void browserPaneStop(BrowserPane *pane) {
  if (pane->scanning) closedir(pane->scanning);
  pane->scanning = NULL;
}

void browserPaneClearHistory(BrowserPane *pane) {
  while (pane->history) {
    BrowserLocation *location = pane->history;
    pane->history = location->previous;
    free(location->path);
    free(location->name);
    free(location);
  }
  pane->historyCount = 0;
}

void browserPaneDestroy(BrowserPane *pane) {
  browserPaneStop(pane);
  clearListing(pane);
  browserPaneClearHistory(pane);
}

int browserPaneOpen(BrowserPane *pane, const char *path) {
  char nextPath[PATH_MAX + 1];
  if (strlen(path) > PATH_MAX) return 0;
  snprintf(nextPath, sizeof(nextPath), "%s", path);
  DIR *directory = opendir(nextPath);
  if (!directory) return 0; // Keep the current location on failure.
  browserPaneStop(pane);
  clearListing(pane);
  snprintf(pane->path, sizeof(pane->path), "%s", nextPath);
  pane->filter[0] = pane->restoreName[0] = '\0';
  pane->selected = pane->restoreSelected = pane->truncated = 0;
  pane->metadataNext = 0;
  pane->scanning = directory;
  return 1;
}

int browserPaneEnter(BrowserPane *pane, const char *name) {
  char path[PATH_MAX + 1];
  if (!pane->path[0] || !name[0] || !strcmp(name, ".") || !strcmp(name, "..") ||
      strpbrk(name, "/\\:") != NULL) return 0;
  int length = snprintf(path, sizeof(path), "%s%s%s", pane->path,
      pane->path[strlen(pane->path) - 1] == '/' ? "" : "/", name);
  if (length < 0 || (size_t)length >= sizeof(path)) return 0;
  BrowserLocation *location = calloc(1, sizeof(*location));
  if (!location) return 0;
  location->path = strdup(pane->path);
  location->name = strdup(name);
  snprintf(location->filter, sizeof(location->filter), "%s", pane->filter);
  location->selected = pane->selected;
  if (!location->path || !location->name || !browserPaneOpen(pane, path)) {
    free(location->path);
    free(location->name);
    free(location);
    return 0;
  }
  // Keep the newest locations if the bounded history is full.
  if (pane->historyCount == BROWSER_HISTORY_MAX) {
    BrowserLocation **oldest = &pane->history;
    while ((*oldest)->previous) oldest = &(*oldest)->previous;
    free((*oldest)->path);
    free((*oldest)->name);
    free(*oldest);
    *oldest = NULL;
    pane->historyCount--;
  }
  location->previous = pane->history;
  pane->history = location;
  pane->historyCount++;
  return 1;
}

int browserPaneUp(BrowserPane *pane, const char *root) {
  char parent[PATH_MAX + 1], child[256];
  size_t rootLength = strlen(root);
  if (!rootLength || !strcmp(pane->path, root) || strlen(pane->path) < rootLength ||
      strncmp(pane->path, root, rootLength) ||
      (root[rootLength - 1] != '/' && pane->path[rootLength] != '/'))
    return 0;
  snprintf(parent, sizeof(parent), "%s", pane->path);
  char *slash = strrchr(parent + rootLength, '/');
  snprintf(child, sizeof(child), "%s", slash ? slash + 1 : parent + rootLength);
  if (slash) *slash = '\0';
  else parent[rootLength] = '\0';
  if (!browserPaneOpen(pane, parent)) return 0;
  BrowserLocation *location = pane->history;
  if (location && !strcmp(location->path, parent)) {
    snprintf(pane->filter, sizeof(pane->filter), "%s", location->filter);
    snprintf(pane->restoreName, sizeof(pane->restoreName), "%s", location->name);
    pane->restoreSelected = location->selected;
    pane->history = location->previous;
    pane->historyCount--;
    free(location->path);
    free(location->name);
    free(location);
  } else {
    snprintf(pane->restoreName, sizeof(pane->restoreName), "%s", child);
    browserPaneClearHistory(pane);
  }
  return 1;
}

int browserPaneRefresh(BrowserPane *pane, const char *selectName) {
  char name[256] = "", filter[BROWSER_FILTER_MAX + 1];
  BrowserEntry *entry = browserPaneEntry(pane, pane->selected);
  snprintf(name, sizeof(name), "%s", selectName ? selectName : entry ? entry->name : "");
  snprintf(filter, sizeof(filter), "%s", pane->filter);
  int selected = pane->selected;
  if (!browserPaneOpen(pane, pane->path)) return 0;
  snprintf(pane->filter, sizeof(pane->filter), "%s", filter);
  snprintf(pane->restoreName, sizeof(pane->restoreName), "%s", name);
  pane->restoreSelected = selected;
  return 1;
}

static void rebuildVisible(BrowserPane *pane) {
  pane->visibleCount = 0;
  for (int i = 0; i < pane->count; i++)
    if (browserNameMatches(pane->entries[i].name, pane->filter))
      pane->visible[pane->visibleCount++] = i;
  if (pane->selected >= pane->visibleCount)
    pane->selected = pane->visibleCount ? pane->visibleCount - 1 : 0;
}

// qsort has no portable context argument. Sorting runs only on the UI thread.
static BrowserSort comparisonSort;
static int compareEntries(const void *left, const void *right) {
  const BrowserEntry *a = left, *b = right;
  if (a->isDirectory != b->isDirectory) return b->isDirectory - a->isDirectory;
  if (!a->isDirectory && comparisonSort != BROWSER_SORT_NAME) {
    if (a->hasSize != b->hasSize) return b->hasSize - a->hasSize;
    if (comparisonSort == BROWSER_SORT_SIZE && a->size != b->size)
      return a->size > b->size ? -1 : 1;
    if (comparisonSort == BROWSER_SORT_DATE && a->modified != b->modified)
      return a->modified > b->modified ? -1 : 1;
  }
  return browserNaturalCompare(a->name, b->name);
}

static void sortListing(BrowserPane *pane) {
  char selectedName[256] = "";
  BrowserEntry *entry = browserPaneEntry(pane, pane->selected);
  if (entry) snprintf(selectedName, sizeof(selectedName), "%s", entry->name);
  comparisonSort = pane->sort;
  if (pane->count > 1) qsort(pane->entries, pane->count, sizeof(*pane->entries), compareEntries);
  rebuildVisible(pane);
  const char *restore = pane->restoreName[0] ? pane->restoreName : selectedName;
  if (pane->restoreName[0])
    pane->selected = pane->restoreSelected < pane->visibleCount ? pane->restoreSelected :
        pane->visibleCount ? pane->visibleCount - 1 : 0;
  for (int i = 0; i < pane->visibleCount; i++)
    if (!strcmp(browserPaneEntry(pane, i)->name, restore)) { pane->selected = i; break; }
  pane->restoreName[0] = '\0';
}

void browserPaneFilter(BrowserPane *pane, const char *filter) {
  snprintf(pane->filter, sizeof(pane->filter), "%s", filter);
  pane->selected = 0;
  pane->restoreName[0] = '\0';
  rebuildVisible(pane);
}

void browserPaneSort(BrowserPane *pane, BrowserSort sort) {
  pane->sort = sort;
  pane->metadataNext = 0;
  if (!pane->scanning && sort == BROWSER_SORT_NAME) sortListing(pane);
}

static int reserveEntry(BrowserPane *pane, size_t nameLength) {
  int capacity = pane->capacity;
  if (pane->count == capacity) capacity += 64;
  if ((size_t)capacity * (sizeof(BrowserEntry) + sizeof(int)) +
      pane->nameBytes + nameLength > BROWSER_MEMORY_BUDGET) return 0;
  if (capacity == pane->capacity) return 1;
  BrowserEntry *entries = realloc(pane->entries, capacity * sizeof(*entries));
  if (!entries) return 0;
  pane->entries = entries;
  int *visible = realloc(pane->visible, capacity * sizeof(*visible));
  if (!visible) return 0;
  pane->visible = visible;
  pane->capacity = capacity;
  return 1;
}

static void loadMetadata(BrowserPane *pane, BrowserEntry *entry) {
  char path[PATH_MAX + 1];
  struct stat info;
  int length = snprintf(path, sizeof(path), "%s%s%s", pane->path,
      pane->path[strlen(pane->path) - 1] == '/' ? "" : "/", entry->name);
  entry->metadataLoaded = 1;
  if (length < 0 || (size_t)length >= sizeof(path)) return;
  entry->hasSize = stat(path, &info) == 0;
  if (entry->hasSize) {
    entry->size = info.st_size > 0 ? (uint64_t)info.st_size : 0;
    entry->modified = info.st_mtime;
    entry->isDirectory = S_ISDIR(info.st_mode);
  }
}

void browserPaneStep(BrowserPane *pane) {
  if (pane->scanning) {
    // Yield to drawing/input after a small batch; no per-file stat for known types.
    for (int batch = 0; batch < 32; batch++) {
      errno = 0;
      struct dirent *item = readdir(pane->scanning);
      if (!item) {
        if (errno) pane->truncated = 2;
        browserPaneStop(pane);
        sortListing(pane);
        break;
      }
      if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
      size_t length = strlen(item->d_name) + 1;
      if (!reserveEntry(pane, length)) {
        pane->truncated = 1;
        browserPaneStop(pane);
        sortListing(pane);
        break;
      }
      BrowserEntry *entry = &pane->entries[pane->count];
      memset(entry, 0, sizeof(*entry));
      entry->name = strdup(item->d_name);
      if (!entry->name) {
        pane->truncated = 1;
        browserPaneStop(pane);
        sortListing(pane);
        break;
      }
      entry->isDirectory = item->d_type == DT_DIR;
      if (item->d_type == DT_UNKNOWN) loadMetadata(pane, entry);
      pane->nameBytes += length;
      if (browserNameMatches(entry->name, pane->filter))
        pane->visible[pane->visibleCount++] = pane->count;
      pane->count++;
    }
    return;
  }
  // Visible rows first. Name browsing avoids stat calls for off-screen files.
  for (int i = pane->selected; i < pane->visibleCount && i < pane->selected + 12; i++) {
    BrowserEntry *entry = browserPaneEntry(pane, i);
    if (!entry->metadataLoaded) { loadMetadata(pane, entry); return; }
  }
  if (pane->sort != BROWSER_SORT_NAME) {
    int loaded = 0;
    while (pane->metadataNext < pane->count && loaded < 8) {
      BrowserEntry *entry = &pane->entries[pane->metadataNext++];
      if (!entry->metadataLoaded) { loadMetadata(pane, entry); loaded++; }
    }
    if (pane->metadataNext < pane->count) return;
    if (pane->metadataNext == pane->count) { sortListing(pane); pane->metadataNext++; }
  }
}
