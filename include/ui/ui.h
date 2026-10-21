#ifndef _UI_H_
#define _UI_H_

#include "target.h"
#include "options.h"
#include "cheats.h"
#include <gsKit.h>
#include <stdint.h>

int uiInit();
uint32_t uiNowMs(void);
int uiLoop(TargetList *titles, int preparedCollectionIdx);
// Returns 0 after a recoverable preflight error, -1 if execution returned.
int uiLaunchTitleWithCheats(Target *target, ArgumentList *arguments,
                           const LunaCheatSettings *cheats);
#ifdef LUNA_ENABLE_PSXCORE
struct LunaPsxVmcSettings;
int uiLaunchPs1Cards(Target *target, const struct LunaPsxVmcSettings *selection,
                     ArgumentList *arguments);
#endif

// Initializes and starts UI splash thread
int startSplashScreen();

// Displays a fatal initialization error in a thread-safe way.
void uiSplashError(const char *str, ...);

// Stops UI splash thread
void stopUISplashThread();

#endif
