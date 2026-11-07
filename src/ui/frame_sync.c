#include "ui/frame_sync.h"
#include <kernel.h>
#include <string.h>
#include <unistd.h>

static int classicVblankSema = -1;
static int classicVblankHandler = -1;
static int classicVblankInitAttempted;
static volatile int classicVblankWaiting;

static int classicVblank(int cause) {
  (void)cause;
  if (classicVblankWaiting) {
    classicVblankWaiting = 0;
    iSignalSema(classicVblankSema);
  }
  ExitHandler();
  return 0;
}

void closeClassicFrameSync(void) {
  // Detach the interrupt before releasing the semaphore it can signal.
  if (classicVblankHandler >= 0)
    gsKit_remove_vsync_handler(classicVblankHandler);
  classicVblankHandler = -1;
  classicVblankWaiting = 0;
  if (classicVblankSema >= 0)
    DeleteSema(classicVblankSema);
  classicVblankSema = -1;
  classicVblankInitAttempted = 0;
}

static int initClassicFrameSync(void) {
  if (classicVblankInitAttempted)
    return classicVblankHandler >= 0;
  classicVblankInitAttempted = 1;
  ee_sema_t semaphore;
  memset(&semaphore, 0, sizeof(semaphore));
  semaphore.max_count = 1;
  classicVblankSema = CreateSema(&semaphore);
  if (classicVblankSema < 0)
    return 0;
  classicVblankHandler = gsKit_add_vsync_handler(classicVblank);
  if (classicVblankHandler < 0) {
    DeleteSema(classicVblankSema);
    classicVblankSema = -1;
    return 0;
  }
  return 1;
}

void syncClassicFrame(GSGLOBAL *gs) {
  if (!gs->FirstFrame) {
    if (!initClassicFrameSync()) {
      gsKit_sync_flip(gs);
      // Resource failure must not leave lower-priority artwork without a turn.
      usleep(3000);
      return;
    }
    // Discard any old signal; only a refresh after this point can flip the
    // completed frame. A signal between arming and WaitSema is retained.
    PollSema(classicVblankSema);
    classicVblankWaiting = 1;
    WaitSema(classicVblankSema);

    // Match gsKit_sync_flip's buffer bookkeeping without its busy vsync wait.
    // The sleeping UI lets artwork run until vblank wakes the higher-priority
    // UI thread. Neither a full PNG decode nor an arbitrary sleep delays input.
    if (gs->DoubleBuffering == GS_SETTING_ON) {
      gsKit_display_buffer(gs);
      gs->ActiveBuffer ^= 1;
    }
  }
  gsKit_setactive(gs);
}
