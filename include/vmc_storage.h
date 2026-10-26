#ifndef LUNA_VMC_STORAGE_H
#define LUNA_VMC_STORAGE_H
#include <stdint.h>

enum { LUNA_VMC_PS1 = 1, LUNA_VMC_PS2 = 2 };
#define LUNA_VMC_PATH 512
typedef struct { uint32_t bytes; } LunaVmcInfo;
typedef void (*LunaVmcProgress)(unsigned percent);
/* Files remain local to the frontend. No runtime or core patching. */
const char *lunaVmcInspect(const char *path, int platform, LunaVmcInfo *info);
const char *lunaVmcReady(const char *path, int platform);
const char *lunaVmcCopy(const char *source, const char *destination, int platform, LunaVmcProgress progress);
const char *lunaVmcBackup(const char *path, int platform, char out[LUNA_VMC_PATH], LunaVmcProgress progress);
const char *lunaVmcReplace(const char *path, const char *source, int platform, LunaVmcProgress progress);
/* Recovery snapshots every candidate, including invalid ones, before mutation. */
const char *lunaVmcRecover(const char *path, const char *suffix, int platform, LunaVmcProgress progress);
const char *lunaVmcCreate(const char *path, int platform, LunaVmcProgress progress);
extern const char *const lunaVmcRecoverySuffixes[9];
#endif
