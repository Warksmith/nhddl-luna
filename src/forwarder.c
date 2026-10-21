#include "common.h"
#include "devices/devices.h"
#include "neutrino.h"
#include "devices/init.h"
#include "options.h"
#include "storage.h"
#include "devices/title_id.h"
#include <stdlib.h>
#include <string.h>

// Quickly forwards the image to Neutrino without loading the UI
int forwardBoot() {
  int res;
  // Forward to Neutrino without loading the UI
  if (!LAUNCHER_OPTIONS.noInit)
    res = initModules(LAUNCHER_OPTIONS.mode);
  else
    res = initModules(MODE_NONE);
  if (res) {
    return res;
  }

  storageConfigure(NULL, LAUNCHER_OPTIONS.mode & STORAGE_SUPPORTED, MODE_NONE, 0);

  int deviceCount = initDeviceMap();
  if (deviceCount <= 0) {
    return -ENODEV;
  }

  if ((res = tryFile(LAUNCHER_OPTIONS.image)) < 0) {
    return -ENOENT;
  }

  if (findNeutrinoELF(NULL)) {
    return -ENOENT;
  }

  Target target = {
      .idx = 0,
      .id = getTitleID(LAUNCHER_OPTIONS.image),
      .fullPath = LAUNCHER_OPTIONS.image,
  };

  char *fileext = strrchr(LAUNCHER_OPTIONS.image, '.');
  if ((fileext != NULL) && (!strcmp(fileext, ".iso") || !strcmp(fileext, ".ISO"))) {
    // Get file name without the extension
    char *isoName = strrchr(LAUNCHER_OPTIONS.image, '/');
    if (!isoName)
      isoName = LAUNCHER_OPTIONS.image;
    else
      isoName++;

    int nameLength = (int)(fileext - isoName);
    target.name = calloc(sizeof(char), nameLength + 1);
    strncpy(target.name, isoName, nameLength);
  }

  for (int i = 0; i < deviceCount; i++)
    if (strstr(LAUNCHER_OPTIONS.image, deviceModeMap[i].mountpoint)) {
      target.device = &deviceModeMap[i];
      break;
    }

  if (!target.device) {
    return -ENODEV;
  }

  // Run the image
  launchTitle(&target, loadLaunchArgumentLists(&target));
  return -ENOENT;
}
