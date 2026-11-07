#ifndef LUNA_UI_FRAME_SYNC_H
#define LUNA_UI_FRAME_SYNC_H

#include <gsKit.h>

// Yield the EE while List waits for the next display refresh.
void syncClassicFrame(GSGLOBAL *gs);
void closeClassicFrameSync(void);

#endif
