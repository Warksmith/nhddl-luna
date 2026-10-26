#ifndef LUNA_MOTION_H
#define LUNA_MOTION_H

/* LUNA Motion: shared movement state and interpolation. */
typedef struct {
  float position[3], previous[3], start[3], target[3], tangent[3];
  float progress, elapsed, duration, coefficient;
  int active;
} LunaMotion;

void lunaMotionReset(LunaMotion *motion);
void lunaMotionRetarget(LunaMotion *motion, const float target[3],
                           float durationMs, float historyScale);
/* One reference 60 Hz tick. Does not change LUNA's existing animation clocks. */
void lunaMotionTick(LunaMotion *motion);

#endif
