#ifndef _DEVICES_H_
#define _DEVICES_H_

#include "common.h"
#include "target.h"

#define MAX_DEVICES 20

// A partial scan owns validated targets but could not inspect every item.
#define TITLE_SCAN_PARTIAL 1

// Returns 0 for a complete scan, TITLE_SCAN_PARTIAL for recoverable omissions,
// or a negative error when results cannot be used (-ENOENT means empty).
typedef int (*titleScanFunc)(TargetList *result, struct DeviceMapEntry *device);
// Must sync the device
typedef void (*syncFunc)();

// Device map entry
struct DeviceMapEntry {
  char *mountpoint;               // Device mountpoint
  syncFunc sync;                  // Must sync the device
  titleScanFunc scan;             // Function used for scanning the device entry for titles. Can be NULL if device must be ignored during scanning. Might not be present in deviceModeMap
  struct DeviceMapEntry *metadev; // If set, cover art and options will be loaded from metadata device instead of this device. Set during initialization
  ModeType mode;                  // Device driver
  uint8_t index;                  // BDM internal device driver number, must be used for passing paths to Neutrino
};

// Contains all available devices.
// Device must be ignored if mode is MODE_ALL or MODE_NONE
extern struct DeviceMapEntry deviceModeMap[];

// Initializes device mode map and returns device count
int initDeviceMap();
// Backends that failed with I/O/allocation errors during the latest discovery.
ModeType deviceDiscoveryFailures(void);
void freeDeviceMapEntries(struct DeviceMapEntry *entries);
void syncDeviceMap(void);

// Uses MMCE devctl calls to switch memory card to given title ID.
// Does nothing unless the GameID preference is enabled. The card holding the
// launcher switches only with switchLauncherCard, once nothing more is read
// from it and IGR will return it to its boot card.
void mmceMountVMC(char *titleID, int switchLauncherCard);
// Slot (0 or 1) of an MMCE card holding the launcher with GameID enabled, or -1.
int mmceLauncherSlot(void);
// LUNA-wide GameID preference, kept in gameid.cfg beside luna.elf. Off by default.
void mmceGameIdConfigure(const char *directory);
int mmceGameIdEnabled(void);
int mmceGameIdSave(int enabled);

// Name of the APA/PFS metadata partition currently mounted at pfs0:.
const char *getMountedPFSPartition(void);

//
// Device-specific scanning functions
//

// Scans given storage device for ISO files and appends valid launch candidates to TargetList
// Uses the complete/partial/error contract of titleScanFunc above.
// Implemented in devices_iso.c
int findISO(TargetList *list, struct DeviceMapEntry *device);

// Scans given APA HDD for HDL partitions and appends valid launch candidates to TargetList
// Returns 0 if successful, non-zero if no targets were found or an error occurs
// Implemented in devices_hdl.c
int findHDLTargets(TargetList *result, struct DeviceMapEntry *device);

#endif
