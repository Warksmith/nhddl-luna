#ifndef LUNA_TEXT_FILE_H
#define LUNA_TEXT_FILE_H
#include <stddef.h>
#include <stdint.h>

#define LUNA_TEXT_BYTES (256U * 1024U)
#define LUNA_TEXT_ROWS 32768U
#define LUNA_TEXT_ROW_BYTES 2048U
typedef enum { LUNA_FILE_OTHER, LUNA_FILE_TEXT, LUNA_FILE_ELF } LunaFileKind;
typedef struct { uint32_t start, length, line; } LunaTextRow;
typedef struct {
  char *text;
  size_t length;
  LunaTextRow *rows;
  uint32_t rowCount;
  int partial;
} LunaTextFile;
typedef int (*LunaTextGlyphWidth)(const char *glyph, void *context);

LunaFileKind lunaFileKind(const char *path);
// Start with a zeroed document. Reading and viewing never modify the source.
const char *lunaTextFileRead(const char *path, LunaTextFile *file,
                            int (*cancel)(void *), void *context);
const char *lunaTextFileLayout(LunaTextFile *file, int width,
                              LunaTextGlyphWidth glyphWidth, void *context);
void lunaTextFileRow(const LunaTextFile *file, uint32_t row, char *out, size_t capacity);
void lunaTextFileFree(LunaTextFile *file);
#endif
