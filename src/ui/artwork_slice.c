#include "ui/artwork_slice.h"
#include <kernel.h>
#include <timer.h>
#include <stdint.h>

#ifndef ARTWORK_SLICE_BUDGET_US
#define ARTWORK_SLICE_BUDGET_US 8000
#endif
static int gate = -1;
static volatile int enabled;
static volatile uint32_t deadline;
static uint32_t frameStart;
static volatile int workers[2] = {-1, -1};


static uint32_t nowUs(void) {
  return (uint32_t)((GetTimerSystemTime() * 1000ULL) / 147456ULL);
}

void artworkSliceDisable(void) {
  enabled = 0;
  if (gate >= 0) {
    SignalSema(gate);
    SignalSema(gate);
  }
}

void artworkSliceFrame(int collectionActive) {
  if (!collectionActive) {
    artworkSliceDisable();
    return;
  }
  if (gate < 0) {
    ee_sema_t sema = {0};
    sema.init_count = 0;
    sema.max_count = 2;
    gate = CreateSema(&sema);
    if (gate < 0) return;
  }
  while (PollSema(gate) == gate) {}
  frameStart = nowUs();
  deadline = frameStart;
  enabled = 1;
}

void artworkSliceGrant(void) {
  if (!enabled) return;
  // Cache maintenance already spent part of this frame's preparation budget.
  // Open only the remainder, so adopting a completed job cannot add another
  // full worker slice on top of an expensive cache update.
  deadline = frameStart + ARTWORK_SLICE_BUDGET_US;
  if ((int32_t)(nowUs() - deadline) < 0)
    SignalSema(gate);
}

void artworkSliceJobBegin(int collectionJob) {
  if (!collectionJob) return;
  int current = GetThreadId();
  for (int i = 0; i < 2; i++) {
    if (workers[i] < 0 || workers[i] == current) {
      workers[i] = current;
      break;
    }
  }
  artworkSliceCheckpoint();
}

void artworkSliceJobEnd(void) {
  int current = GetThreadId();
  for (int i = 0; i < 2; i++)
    if (workers[i] == current) workers[i] = -1;
}

void artworkSliceCheckpoint(void) {
  if (!enabled) return;
  int current = GetThreadId();
  if (workers[0] != current && workers[1] != current) return;
  while (enabled && (int32_t)(nowUs() - deadline) >= 0) {
    int result = WaitSema(gate);
    if (result < 0) return;
  }
}
