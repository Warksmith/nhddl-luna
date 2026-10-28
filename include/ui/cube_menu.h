#ifndef LUNA_CUBE_MENU_H
#define LUNA_CUBE_MENU_H
enum { LUNA_CUBE_BACK, LUNA_CUBE_VMC, LUNA_CUBE_STORAGE,
       LUNA_CUBE_LANGUAGE, LUNA_CUBE_EXIT, LUNA_CUBE_SHUTDOWN, LUNA_CUBE_DEFAULTS };
typedef struct {
  const char *title;
  int count;
  const char *(*label)(void *context, int row);
  const char *(*value)(void *context, int row);
  const char *(*description)(void *context, int row);
  void (*adjust)(void *context, int row, int direction);
  void *context;
  const char *status;
} LunaCubePage;
typedef struct {
  void *context;
  void (*draw)(void *context, float opacity);
  int (*input)(void *context, int buttons);
} LunaCubeWorkspace;
void uiCubeWorkspaceLoop(const LunaCubeWorkspace *workspace);
enum { LUNA_CUBE_SUBMENU_STAY, LUNA_CUBE_SUBMENU_BACK, LUNA_CUBE_SUBMENU_DONE };
typedef struct {
  void *context;
  void (*begin)(void *context);
  void (*draw)(void *context, float opacity);
  int (*input)(void *context, int buttons);
  void (*end)(void *context);
} LunaCubeSubmenu;
typedef struct {
  int selected,first,fromY,toY;
  unsigned int startMs;
  int glideMs;
} LunaCubePageFocus;
// Draw page contents into the shared settings scene without presenting a frame.
void uiCubeSubmenuPageDraw(const LunaCubePage *page, int selected,
                         LunaCubePageFocus *focus, float opacity);
void uiCubeSubmenuFocusDraw(int left, int top, int right, int bottom, float opacity);
// The first four settings rows supply VMC, Storage, Language, and Game Defaults.
int uiCubeSettingsLoop(int *selected, const char *status,
                      const LunaCubeSubmenu submenus[4]);
#endif
