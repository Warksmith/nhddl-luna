#ifndef LUNA_PSBBN_MOTION_H
#define LUNA_PSBBN_MOTION_H

/* Local reconstruction; no Sony code, assets or interpreter is linked. */
typedef struct {
  float position[3], previous[3], start[3], target[3], tangent[3];
  float progress, elapsed, duration, coefficient;
  int active;
} LunaPsbbnMotion;

void lunaPsbbnMotionReset(LunaPsbbnMotion *motion);
void lunaPsbbnMotionRetarget(LunaPsbbnMotion *motion, const float target[3],
                           float durationMs, float historyScale);
/* One reference 60 Hz tick. Does not change LUNA's existing animation clocks. */
void lunaPsbbnMotionTick(LunaPsbbnMotion *motion);

#endif
