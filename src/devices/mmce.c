// Implements support for MMCE devices
#include "common.h"
#include "devices/devices.h"
#include "options.h"
#include "storage.h"
#include "ui/ui.h"
#include <errno.h>
#include <dirent.h>
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Used to make devctl calls
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

static char mmceMountpoint[] = "mmceX:";
// LUNA-wide preference stored beside luna.elf; off unless saved as "on".
static char gameIdPath[PATH_MAX + 1];
static int gameIdEnabled;

static int readGameIdSetting(const char *path, int *enabled) {
  FILE *file = fopen(path, "r");
  if (!file) return 0;
  char value[8];
  int readable = fgets(value, sizeof(value), file) != NULL;
  fclose(file);
  if (!readable) return 0;
  value[strcspn(value, "\r\n")] = '\0';
  if (!strcmp(value, "on")) *enabled = 1;
  else if (!strcmp(value, "off")) *enabled = 0;
  else return 0;
  return 1;
}

void mmceGameIdConfigure(const char *directory) {
  gameIdEnabled = 0;
  gameIdPath[0] = '\0';
  if (!directory || !directory[0]) return;
  int n = snprintf(gameIdPath, sizeof(gameIdPath), "%s%sgameid.cfg", directory,
                   directory[strlen(directory) - 1] == '/' ? "" : "/");
  if (n < 0 || (size_t)n >= sizeof(gameIdPath)) { gameIdPath[0] = '\0'; return; }
  // Readers accept the staged record if a commit copy was interrupted.
  char temp[PATH_MAX + 8];
  snprintf(temp, sizeof(temp), "%s.tmp", gameIdPath);
  if (!readGameIdSetting(gameIdPath, &gameIdEnabled)) readGameIdSetting(temp, &gameIdEnabled);
}

int mmceGameIdEnabled(void) { return gameIdEnabled; }

int mmceGameIdSave(int enabled) {
  enabled = enabled != 0;
  if (enabled == gameIdEnabled) return 0;
  if (!gameIdPath[0]) return -EROFS;
  char temp[PATH_MAX + 8];
  snprintf(temp, sizeof(temp), "%s.tmp", gameIdPath);
  FILE *file = fopen(temp, "w");
  if (!file) return -EIO;
  int bad = fprintf(file, "%s\n", enabled ? "on" : "off") < 0;
  if (fclose(file)) bad = 1;
  if (bad) { remove(temp); return -EIO; }
  int result = commitConfigFile(temp, gameIdPath);
  if (result) return result;
  gameIdEnabled = enabled;
  return 0;
}

// Initializes map entries for MMCE devices
int initMMCEDevices(int newDeviceIdx) {
  DIR *directory;

  int deviceCount = 0;
  for (int i = 0; i < 2 && newDeviceIdx < MAX_DEVICES; i++) {
    mmceMountpoint[4] = i + '0';

    directory = opendir(mmceMountpoint);
    if (directory != NULL) {
      closedir(directory);

      deviceModeMap[newDeviceIdx].mode = MODE_MMCE;
      deviceModeMap[newDeviceIdx].index = i;
      deviceModeMap[newDeviceIdx].mountpoint = strdup(mmceMountpoint);
      if (!deviceModeMap[newDeviceIdx].mountpoint) return -ENOMEM;
      deviceModeMap[newDeviceIdx].scan = &findISO;

      deviceCount++;
      newDeviceIdx++;
    }
  }
  return deviceCount;
}

int mmceLauncherSlot(void) {
  if (!gameIdEnabled || strncmp(NEUTRINO_ELF_PATH, "mc", 2) ||
      (NEUTRINO_ELF_PATH[2] != '0' && NEUTRINO_ELF_PATH[2] != '1') || NEUTRINO_ELF_PATH[3] != ':')
    return -1;
  mmceMountpoint[4] = NEUTRINO_ELF_PATH[2];
  if (fileXioDevctl(mmceMountpoint, 0x1, NULL, 0, NULL, 0) < 0)
    return -1;
  return NEUTRINO_ELF_PATH[2] - '0';
}

// Uses MMCE devctl calls to switch memory card to given title ID.
// mmceman is resident in every mode except MX4SIO, so this does not depend on
// MMCE being enabled as a game source.
void mmceMountVMC(char *titleID, int switchLauncherCard) {
  if (!gameIdEnabled || !titleID || !titleID[0])
    return;
  char mcMountpoint[] = "mcX:";
  // Send GameID to both MMCE devices
  for (int i = '0'; i < '2'; i++) {
    mcMountpoint[2] = i;
    mmceMountpoint[4] = i;

    // Neutrino and the IGR return still read the launcher's card
    if (!switchLauncherCard && !strncmp(mcMountpoint, NEUTRINO_ELF_PATH, 3)) {
      continue;
    }

    // Ping MMCE first to make sure the device is present
    if (fileXioDevctl(mmceMountpoint, 0x1, NULL, 0, NULL, 0) < 0)
      continue;

    if ((fileXioDevctl(mmceMountpoint, 0x8, titleID, (strlen(titleID) + 1), NULL, 0)) < 0) {
      // Skip polling if devctl call has failed
      continue;
    }

    // Poll MMCE status until busy bit is clear. The switch usually completes
    // at once, so check every 20 ms (after 100 ms for the busy bit to be set)
    // instead of every second, still giving up after 15 seconds.
    usleep(100000);
    for (int i = 0; i < 745; i++) {
      if ((fileXioDevctl(mmceMountpoint, 0x2, NULL, 0, NULL, 0) & 1) == 0) {
        break;
      }
      usleep(20000);
    }
  }
}
