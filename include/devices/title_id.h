#ifndef _DEVICES_TITLE_ID_H_
#define _DEVICES_TITLE_ID_H_

// Loads SYSTEM.CNF from ISO and extracts title ID
// On NULL, errno distinguishes invalid metadata (EINVAL) from I/O (EIO)
// and allocation (ENOMEM) failures. Callers must not publish partial rescans.
char *getTitleID(char *path);

#endif
