#ifndef LUNA_UI_OPTIONS_MENU_H
#define LUNA_UI_OPTIONS_MENU_H

#include "target.h"
#include <stdint.h>

typedef struct {
  // Borrowed scene; ownership stays with the library.
  void (*drawLibrary)(void *context, float offsetY);
  void *context;
} LunaOptionsScene;

int uiTitleOptionsLoop(Target *target, const LunaOptionsScene *scene);

#endif
