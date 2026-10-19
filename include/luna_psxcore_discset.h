// LUNA owns grouping and image identity; PSXCore owns game-time swapping.
#ifndef LUNA_PSXCORE_DISCSET_H
#define LUNA_PSXCORE_DISCSET_H
#include "target.h"
#include "psxcore_discset.h"
#include <stddef.h>
int lunaPsxPlaylistFilename(const char *path);
const char *lunaPsxPlaylistParse(const void *text, size_t bytes,
    const char *playlistRelativePath, TargetDiscSet *out);
const char *lunaPsxPlaylistRead(const char *path, TargetDiscSet *out);
int lunaPsxGroupTargets(TargetList *targets);
// Identity for cards/settings stays fixed when the starting disc changes.
const char *lunaPsxGameIdentity(const Target *target, char out[LUNA_DISC_PATH_BYTES]);
const char *lunaPsxDiscSetMeasure(const Target *target, unsigned initial,
    NpFileDiscSet *out, int (*progress)(uint64_t, uint64_t));
// Writers publish verified files; a failed write never authorizes launch.
const char *lunaPsxDiscSetPublish(const char *root, const NpFileDiscSet *set,
    char relativePath[LUNA_DISC_PATH_BYTES]);
const char *lunaPsxPlaylistSave(const char *path, const TargetDiscSet *set, int replace);
// Stored complete-image fingerprints belong to LUNA, outside the core ABI.
const char *lunaPsxDiscIdentityCheck(const char *root, const char *key,
    const char *path, uint64_t bytes, const unsigned char sha256[32]);
const char *lunaPsxDiscIdentityReset(const char *root, const char *key, const char *path);
// Conservative filename suggestions in one directory, ordered by disc marker.
const char *lunaPsxDiscSuggest(const Target *target, TargetDiscSet *out);
#endif
