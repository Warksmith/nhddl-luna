// Gets title ID from PS2 DVD
#include "devices/title_id.h"
#include "dprintf.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <ps2sdkapi.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// This code is based on isofs.irx and title ID parsing code from Neutrino.
#define SECTOR_SIZE 2048
#define TOC_LBA 16
#define SYSTEM_CNF_NAME "SYSTEM.CNF;1"
#define DIRECTORY_HEADER_SIZE 33
#define MAX_SYSTEM_CNF_SIZE (64U * 1024U)

typedef struct {
  uint64_t offset;
  uint32_t size;
} ISOExtent;

static unsigned char iso_buf[SECTOR_SIZE];

// ISO records need not be aligned for native integer loads.
static uint32_t readLE32(const unsigned char *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
         (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int readExact(int fd, void *buffer, size_t size) {
  unsigned char *next = buffer;
  while (size) {
    ssize_t count = read(fd, next, size);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      return -EIO;
    next += count;
    size -= count;
  }
  return 0;
}

static int validRecord(const unsigned char *record, size_t available) {
  return available >= DIRECTORY_HEADER_SIZE &&
         record[0] >= DIRECTORY_HEADER_SIZE && record[0] <= available &&
         record[32] > 0 && record[32] <= record[0] - DIRECTORY_HEADER_SIZE;
}

static int recordExtent(const unsigned char *record, uint64_t isoSize,
                         ISOExtent *extent) {
  // Skip extended-attribute blocks, when present, to reach the file data.
  extent->offset = ((uint64_t)readLE32(record + 2) + record[1]) * SECTOR_SIZE;
  extent->size = readLE32(record + 10);
  return extent->size && extent->offset <= isoSize &&
         extent->size <= isoSize - extent->offset;
}

static int getPVD(int fd, uint64_t isoSize, ISOExtent *root) {
  if (isoSize < (uint64_t)(TOC_LBA + 1) * SECTOR_SIZE) return -EINVAL;
  if (lseek64(fd, (int64_t)TOC_LBA * SECTOR_SIZE, SEEK_SET) < 0 ||
      readExact(fd, iso_buf, sizeof(iso_buf)))
    return -EIO;
  if (iso_buf[0] != 1 || memcmp(iso_buf + 1, "CD001", 5) || iso_buf[6] != 1)
    return -EINVAL;
  const unsigned char *record = iso_buf + 0x9c;
  if (!validRecord(record, sizeof(iso_buf) - 0x9c) || !(record[25] & 2) ||
      !recordExtent(record, isoSize, root))
    return -EINVAL;
  return 0;
}

static int getTOCEntry(int fd, const ISOExtent *root, uint64_t isoSize,
                       ISOExtent *systemCNF) {
  uint32_t remaining = root->size;
  uint64_t offset = root->offset;
  while (remaining) {
    size_t bytes = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE;
    if (lseek64(fd, (int64_t)offset, SEEK_SET) < 0 ||
        readExact(fd, iso_buf, bytes))
      return -EIO;
    for (size_t pos = 0; pos < bytes;) {
      const unsigned char *record = iso_buf + pos;
      if (!record[0])
        break; // Padding ends this sector, not the entire directory.
      if (!validRecord(record, bytes - pos))
        return -EINVAL;
      if (record[32] == sizeof(SYSTEM_CNF_NAME) - 1 &&
          !memcmp(record + DIRECTORY_HEADER_SIZE, SYSTEM_CNF_NAME,
                  sizeof(SYSTEM_CNF_NAME) - 1)) {
        if ((record[25] & (2 | 0x80)) ||
            !recordExtent(record, isoSize, systemCNF))
          return -EINVAL;
        return 0;
      }
      pos += record[0];
    }
    remaining -= bytes;
    offset += bytes;
  }
  return -ENOENT;
}

char *getTitleID(char *path) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    DPRINTF("WARN: %s: Failed to open ISO\n", path);
    errno = EIO;
    return NULL;
  }
  ISOExtent root, systemCNF;
  char *text = NULL, *titleID = NULL;
  int error = -EINVAL;
  int64_t isoSize = lseek64(fd, 0, SEEK_END);
  if (isoSize < 0) { error = -EIO; goto done; }
  error = getPVD(fd, (uint64_t)isoSize, &root);
  if (!error) error = getTOCEntry(fd, &root, (uint64_t)isoSize, &systemCNF);
  if (error || systemCNF.size > MAX_SYSTEM_CNF_SIZE) {
    DPRINTF("WARN: %s: Invalid ISO metadata or SYSTEM.CNF extent\n", path);
    if (error != -EIO) error = -EINVAL;
    goto done;
  }
  text = malloc((size_t)systemCNF.size + 1);
  if (!text) { error = -ENOMEM; goto done; }
  if (lseek64(fd, (int64_t)systemCNF.offset, SEEK_SET) < 0 ||
      readExact(fd, text, systemCNF.size)) {
    DPRINTF("WARN: %s: Failed to read SYSTEM.CNF\n", path);
    error = -EIO;
    goto done;
  }
  text[systemCNF.size] = '\0';
  error = -EINVAL;

  const char *boot2 = strstr(text, "BOOT2");
  if (!boot2)
    goto done;
  const char *lineEnd = boot2 + strcspn(boot2, "\r\n");
  const char *filename = strstr(boot2, "cdrom0:");
  if (!filename || filename >= lineEnd)
    goto done;
  filename += 7;
  if (filename >= lineEnd || (*filename != '\\' && *filename != '/'))
    goto done;
  filename++;
  // The eleven-byte title ID and its version suffix must fit in this line.
  if (lineEnd - filename < 13 || filename[11] != ';' ||
      filename[12] < '0' || filename[12] > '9')
    goto done;
  titleID = malloc(12);
  if (titleID) {
    memcpy(titleID, filename, 11);
    titleID[11] = '\0';
  } else error = -ENOMEM;
done:
  free(text);
  if (close(fd) < 0) {
    free(titleID);
    titleID = NULL;
    error = -EIO;
  }
  errno = titleID ? 0 : -error;
  return titleID;
}
