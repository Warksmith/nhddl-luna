#ifndef LUNA_CAMERA_TRANSITIONS_H
#define LUNA_CAMERA_TRANSITIONS_H

#include <stdint.h>
#include "ui/graphics.h"
void lunaCameraSceneBegin(int open, uint32_t nowMs);
void lunaCameraSceneBeforeBackground(void);
void lunaCameraSceneAfterBackground(void);
void lunaCameraSceneEnd(void);
void lunaCameraMenuReturnStart(uint32_t nowMs);
void lunaCameraMenuReturnCancel(void);
void lunaCameraClassicBounds(float rect[4]);
void lunaCameraViewStart(GSTEXTURE *cover, int target, const float rect[4]);
void lunaCameraViewCancel(void);
void lunaCameraViewCheck(int isCollection, int target);
int lunaCameraViewActive(void);
float lunaCameraViewProgress(void);
int lunaCameraViewTurnBack(void);
float lunaCameraViewTurnWidth(void);
GSTEXTURE *lunaCameraViewSource(void);
void lunaCameraViewReady(int ready);
void lunaCameraViewCoverRect(float x, float y, float size, float rect[4]);
void lunaCameraViewNeighbor(float anchorX, float anchorY,
                           float *x, float *y, float *size);
void lunaCameraChannelStart(int target);
void lunaCameraChannelCancel(void);
void lunaCameraChannelCheck(int isOrbit, int target);
int lunaCameraChannelActive(void);
float lunaCameraChannelProgress(void);
float lunaCameraChannelBlend(void);
void lunaCameraChannelPoint(float sourceX, float sourceY, float *x, float *y);
void lunaCameraCollectionBeginFrame(int target);
int lunaCameraCollectionHasFrame(int target);
int lunaCameraCollectionCoverRect(int cacheIdx, float rect[4], int *emphasis, int *visibility);


#endif
