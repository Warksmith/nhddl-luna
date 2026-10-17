#include "ui/text_file.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sameExtension(const char *a, const char *b) {
  while (*a && *b && tolower((unsigned char)*a) == (unsigned char)*b) { a++; b++; }
  return !*a && !*b;
}
LunaFileKind lunaFileKind(const char *path) {
  static const char *const text[] = {
    ".txt", ".ini", ".cfg", ".conf", ".cnf", ".log", ".yaml", ".yml",
    ".json", ".xml", ".csv", ".tsv", ".md", ".nfo", ".lua", ".cht"
  };
  if (!path) return LUNA_FILE_OTHER;
  const char *extension = strrchr(path, '.');
  const char *slash = strrchr(path, '/');
  if (!extension || (slash && extension < slash)) return LUNA_FILE_OTHER;
  if (sameExtension(extension, ".elf")) return LUNA_FILE_ELF;
  for (size_t i = 0; i < sizeof(text) / sizeof(text[0]); i++)
    if (sameExtension(extension, text[i])) return LUNA_FILE_TEXT;
  return LUNA_FILE_OTHER;
}

// Number of bytes in one valid UTF-8 scalar, excluding surrogates/overlong forms.
static size_t scalarBytes(const unsigned char *p, size_t available) {
  if (!available) return 0;
  if (*p < 128) return 1;
  size_t count = *p >= 0xc2 && *p <= 0xdf ? 2 :
      *p >= 0xe0 && *p <= 0xef ? 3 : *p >= 0xf0 && *p <= 0xf4 ? 4 : 0;
  if (!count || count > available) return 0;
  for (size_t i = 1; i < count; i++) if ((p[i] & 0xc0) != 0x80) return 0;
  if ((p[0] == 0xe0 && p[1] < 0xa0) || (p[0] == 0xed && p[1] >= 0xa0) ||
      (p[0] == 0xf0 && p[1] < 0x90) || (p[0] == 0xf4 && p[1] >= 0x90)) return 0;
  return count;
}

void lunaTextFileFree(LunaTextFile *file) {
  free(file->text);
  free(file->rows);
  memset(file, 0, sizeof(*file));
}

const char *lunaTextFileRead(const char *path, LunaTextFile *file,
                            int (*cancel)(void *), void *context) {
  FILE *input = fopen(path, "rb");
  if (!input) return "Could not open this text file.";
  LunaTextFile result = {0};
  result.text = malloc(LUNA_TEXT_BYTES + 1);
  const char *error = result.text ? NULL : "Not enough memory to view this file.";
  while (!error && result.length < LUNA_TEXT_BYTES) {
    if (cancel && cancel(context)) { error = "Text preview cancelled."; break; }
    size_t count = fread(result.text + result.length, 1, 32768, input);
    result.length += count;
    if (count < 32768) break;
  }
  if (!error) {
    result.partial = result.length == LUNA_TEXT_BYTES && fgetc(input) != EOF;
    if (ferror(input)) error = "Could not read this text file.";
  }
  if (fclose(input) && !error) error = "Could not read this text file.";
  if (!error && result.length >= 2 &&
      ((!memcmp(result.text, "\xff\xfe", 2)) || !memcmp(result.text, "\xfe\xff", 2)))
    error = "UTF-16 text is not supported. Use UTF-8.";
  size_t read = result.length >= 3 && !memcmp(result.text, "\xef\xbb\xbf", 3) ? 3 : 0;
  size_t written = 0;
  while (!error && read < result.length) {
    unsigned char value = (unsigned char)result.text[read];
    if ((value < 32 && value != '\r' && value != '\n' && value != '\t') || value == 127) {
      error = "This file appears to contain binary data.";
      break;
    }
    if (value == '\r') {
      read++;
      if (read < result.length && result.text[read] == '\n') read++;
      result.text[written++] = '\n';
    } else {
      size_t bytes = scalarBytes((unsigned char *)result.text + read, result.length - read);
      if (!bytes) { result.text[written++] = '?'; read++; }
      else { memmove(result.text + written, result.text + read, bytes); written += bytes; read += bytes; }
    }
  }
  if (error) { lunaTextFileFree(&result); return error; }
  result.length = written;
  result.text[written] = '\0';
  lunaTextFileFree(file);
  *file = result;
  return NULL;
}

static int appendRow(LunaTextFile *file, uint32_t *capacity,
                      size_t start, size_t length, uint32_t line) {
  if (file->rowCount == LUNA_TEXT_ROWS) { file->partial = 1; return 0; }
  if (file->rowCount == *capacity) {
    uint32_t next = *capacity + 128;
    LunaTextRow *rows = realloc(file->rows, next * sizeof(*rows));
    if (!rows) return -1;
    file->rows = rows;
    *capacity = next;
  }
  file->rows[file->rowCount++] = (LunaTextRow){(uint32_t)start, (uint32_t)length, line};
  return 1;
}

const char *lunaTextFileLayout(LunaTextFile *file, int width,
                              LunaTextGlyphWidth glyphWidth, void *context) {
  free(file->rows);
  file->rows = NULL;
  file->rowCount = 0;
  if (!file->text || width < 1 || !glyphWidth) return "Could not lay out this text file.";
  uint32_t capacity = 0, line = 1, column = 0;
  size_t start = 0, position = 0;
  int used = 0, result = 1;
  while (position < file->length && result > 0) {
    if (file->text[position] == '\n') {
      result = appendRow(file, &capacity, start, position - start, line++);
      start = ++position;
      used = 0;
      column = 0;
      continue;
    }
    size_t bytes = scalarBytes((unsigned char *)file->text + position, file->length - position);
    if (!bytes) bytes = 1;
    char glyph[5] = {0};
    memcpy(glyph, file->text + position, bytes);
    int tab = glyph[0] == '\t';
    int columns = tab ? 4 - (int)(column % 4) : 1;
    int advance = glyphWidth(tab ? " " : glyph, context);
    if (advance < 0) advance = 0;
    if (advance > 0x10000) advance = 0x10000;
    advance *= columns;
    if (position > start && (advance > width - used || position - start + bytes > 512)) {
      result = appendRow(file, &capacity, start, position - start, line);
      start = position;
      used = 0;
      column = 0;
      continue;
    }
    used += advance;
    column += columns;
    position += bytes;
  }
  if (result > 0) result = appendRow(file, &capacity, start, position - start, line);
  return result < 0 ? "Not enough memory to view this file." : NULL;
}

void lunaTextFileRow(const LunaTextFile *file, uint32_t row, char *out, size_t capacity) {
  if (!capacity) return;
  out[0] = '\0';
  if (row >= file->rowCount) return;
  const LunaTextRow *entry = &file->rows[row];
  size_t position = entry->start, end = position + entry->length, written = 0;
  unsigned column = 0;
  while (position < end) {
    if (file->text[position] == '\t') {
      unsigned spaces = 4 - column % 4;
      if (spaces >= capacity - written) break;
      memset(out + written, ' ', spaces);
      written += spaces;
      column += spaces;
      position++;
    } else {
      size_t bytes = scalarBytes((unsigned char *)file->text + position, end - position);
      if (!bytes) bytes = 1;
      if (bytes >= capacity - written) break;
      memcpy(out + written, file->text + position, bytes);
      written += bytes;
      position += bytes;
      column++;
    }
  }
  out[written] = '\0';
}
