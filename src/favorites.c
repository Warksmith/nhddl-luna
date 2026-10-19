// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "favorites.h"
#include "common.h"
#include "devices/devices.h"
#include "dprintf.h"
#include "options.h"
#include <errno.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char favoritesPath[] = "/favorites.txt";
static const char favoritesTempPath[] = "/favorites.txt.tmp";
static const char favoritesComplete[] = "# LUNA_FAVORITES_COMPLETE\n";

static struct DeviceMapEntry *favoriteDevice(Target *target) {
  return target->device->metadev ? target->device->metadev : target->device;
}

static const char *relativeTargetPath(Target *target) {
  // Synthetic identity is stable across starting-disc and playlist-name changes.
  static char setPath[64];
  if (target->discs && target->discs->key[0]) {
    snprintf(setPath, sizeof(setPath), "/@PS1SET/%s", target->discs->key);
    return setPath;
  }
  int relativeIndex = getRelativePathIdx(target->fullPath);
  return target->fullPath + ((relativeIndex < 0) ? 0 : relativeIndex);
}

static int deviceWasVisited(TargetList *titles, Target *stop, struct DeviceMapEntry *device) {
  Target *target = titles->first;
  while (target != NULL && target != stop) {
    if (favoriteDevice(target) == device)
      return 1;
    target = target->next;
  }
  return 0;
}

// Only a complete staged record may override the primary file. A save interrupted
// while writing its list must not hide the previous selection with a valid prefix.
static int readFavoriteFile(const char *path, int staged, TargetList *titles,
                             struct DeviceMapEntry *device, uint8_t *flags,
                             size_t flagCount) {
  FILE *file = fopen(path, "r");
  if (!file)
    return errno == ENOENT ? -ENOENT : -EIO;
  char favorite[PATH_MAX + 2];
  int complete = 0, result = 0;
  while (fgets(favorite, sizeof(favorite), file)) {
    if (!strcmp(favorite, favoritesComplete)) {
      complete = 1;
      if (fgetc(file) != EOF)
        result = -EINVAL;
      break;
    }
    size_t length = strcspn(favorite, "\r\n");
    if (length == sizeof(favorite) - 1 ||
        (!strchr(favorite, '\n') && !feof(file))) {
      result = -EINVAL;
      break;
    }
    favorite[length] = '\0';
    if (!length || !titles || !flags)
      continue;
    for (Target *target = titles->first; target; target = target->next)
      if (target->idx < flagCount && favoriteDevice(target) == device) {
        int match = !strcmp(favorite, relativeTargetPath(target));
        if (target->discs) for (unsigned disc=0; disc<target->discs->count; ++disc)
          if (!strcmp(favorite, target->discs->paths[disc])) match=1;
        if (match) flags[target->idx] = 1;
      }
  }
  if (ferror(file))
    result = -EIO;
  if (fclose(file))
    result = -EIO;
  if (!result && staged && !complete)
    result = -EINVAL;
  return result;
}

int loadFavoriteFlags(TargetList *titles, uint8_t *flags, size_t flagCount) {
  Target *deviceTarget;
  int loaded = 0;

  if (titles == NULL || flags == NULL || flagCount < (size_t)titles->total)
    return -EINVAL;
  memset(flags, 0, flagCount);
  uint8_t *candidate = calloc(flagCount ? flagCount : 1, 1);
  if (!candidate)
    return -ENOMEM;

  for (deviceTarget = titles->first; deviceTarget != NULL; deviceTarget = deviceTarget->next) {
    struct DeviceMapEntry *device = favoriteDevice(deviceTarget);
    char path[PATH_MAX];
    if (deviceWasVisited(titles, deviceTarget, device))
      continue;
    const char *paths[] = {favoritesTempPath, favoritesPath};
    for (int attempt = 0; attempt < 2; attempt++) {
      memset(candidate, 0, flagCount);
      if (buildConfigFilePath(path, sizeof(path), device->mountpoint, paths[attempt]) ||
          readFavoriteFile(path, attempt == 0, titles, device, candidate, flagCount))
        continue;
      for (size_t i = 0; i < flagCount; i++)
        if (candidate[i] && !flags[i]) { flags[i] = 1; loaded++; }
      break;
    }
  }
  free(candidate);
  return loaded;
}

int saveFavoriteFlags(TargetList *titles, const uint8_t *flags, size_t flagCount,
                      Target *selectedTitle) {
  struct DeviceMapEntry *device;
  Target *target;
  char directory[PATH_MAX];
  char path[PATH_MAX];
  char tempPath[PATH_MAX];
  FILE *file;
  struct stat st;

  if (titles == NULL || flags == NULL || selectedTitle == NULL ||
      flagCount < (size_t)titles->total)
    return -EINVAL;
  device = favoriteDevice(selectedTitle);
  if (buildConfigFilePath(directory, sizeof(directory), device->mountpoint, NULL) ||
      buildConfigFilePath(path, sizeof(path), device->mountpoint, favoritesPath) ||
      buildConfigFilePath(tempPath, sizeof(tempPath), device->mountpoint, favoritesTempPath))
    return -ENAMETOOLONG;

  if (stat(directory, &st) == -1 && mkdir(directory, 0777)) {
    DPRINTF("ERROR: Failed to create favorites directory: %d\n", errno);
    return -EIO;
  }
  // Do not truncate the only complete copy left by an earlier failed commit.
  int recoveryResult = readFavoriteFile(tempPath, 1, NULL, NULL, NULL, 0);
  if ((recoveryResult && recoveryResult != -ENOENT && recoveryResult != -EINVAL) ||
      (!recoveryResult && commitConfigFile(tempPath, path)))
    return -EIO;
  file = fopen(tempPath, "w");
  if (file == NULL) {
    DPRINTF("ERROR: Failed to open favorites file: %d\n", errno);
    return -EIO;
  }

  for (target = titles->first; target != NULL; target = target->next) {
    if (target->idx < flagCount && flags[target->idx] && favoriteDevice(target) == device &&
        fprintf(file, "%s\n", relativeTargetPath(target)) < 0) {
      fclose(file);
      remove(tempPath);
      return -EIO;
    }
  }
  int completeResult = fputs(favoritesComplete, file);
  if (fclose(file) || completeResult == EOF) {
    remove(tempPath);
    return -EIO;
  }

  if (commitConfigFile(tempPath, path)) {
    DPRINTF("ERROR: Failed to commit favorites file: %d\n", errno);
    return -EIO;
  }
  return 0;
}

// The source library owns games; a filtered view owns only its pointer array.
TargetList *buildFavoriteTargetList(TargetList *titles, const uint8_t *flags,
                                    size_t flagCount) {
  if (!titles || !flags || flagCount < (size_t)titles->total) return NULL;
  TargetList *favorites = calloc(1, sizeof(*favorites));
  if (!favorites) return NULL;
  favorites->borrowed = 1;
  favorites->indexCapacity = titles->total;
  if (titles->total) {
    favorites->byIndex = malloc((size_t)titles->total * sizeof(*favorites->byIndex));
    if (!favorites->byIndex) { freeTargetList(favorites); return NULL; }
  }
  for (Target *source = titles->first; source; source = source->next)
    if (flags[source->idx]) favorites->byIndex[favorites->total++] = source;
  favorites->first = favorites->total ? favorites->byIndex[0] : NULL;
  favorites->last = favorites->total ? favorites->byIndex[favorites->total - 1] : NULL;
  return favorites;
}

static int favoriteInsertionRank(const TargetList *favorites, int originalIndex) {
  int low = 0, high = favorites->total;
  while (low < high) {
    int middle = low + (high - low) / 2;
    if (favorites->byIndex[middle]->idx < originalIndex) low = middle + 1;
    else high = middle;
  }
  return low;
}

int favoriteTargetIndex(const TargetList *favorites, int originalIndex) {
  int rank = favoriteInsertionRank(favorites, originalIndex);
  return rank < favorites->total && favorites->byIndex[rank]->idx == originalIndex
             ? rank : -1;
}

int setFavoriteTarget(TargetList *favorites, Target *source, int enabled) {
  if (!favorites || !favorites->borrowed || !source) return -EINVAL;
  int rank = favoriteInsertionRank(favorites, source->idx);
  int present = rank < favorites->total && favorites->byIndex[rank] == source;
  if (!!enabled == present) return 0;
  if (enabled) {
    if (favorites->total >= favorites->indexCapacity) return -ENOMEM;
    memmove(favorites->byIndex + rank + 1, favorites->byIndex + rank,
            (size_t)(favorites->total - rank) * sizeof(*favorites->byIndex));
    favorites->byIndex[rank] = source;
    favorites->total++;
  } else {
    memmove(favorites->byIndex + rank, favorites->byIndex + rank + 1,
            (size_t)(favorites->total - rank - 1) * sizeof(*favorites->byIndex));
    favorites->byIndex[--favorites->total] = NULL;
  }
  favorites->first = favorites->total ? favorites->byIndex[0] : NULL;
  favorites->last = favorites->total ? favorites->byIndex[favorites->total - 1] : NULL;
  return 0;
}
