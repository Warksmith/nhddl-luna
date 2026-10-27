#include "ui/artwork_slice.h"
#include <kernel.h>
#include <timer.h>
#include <stdint.h>
#include <stddef.h>

#ifndef ARTWORK_SLICE_BUDGET_US
#define ARTWORK_SLICE_BUDGET_US 8000
#endif
static int gate = -1;
static volatile int enabled;
static volatile uint32_t deadline;
static uint32_t frameStart;
static volatile int workers[2] = {-1, -1};
static const volatile int *volatile workerCancelled[2];


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

int artworkSliceJobBegin(int collectionJob, const volatile int *cancelled) {
  if (!collectionJob) return 0;
  int current = GetThreadId();
  for (int i = 0; i < 2; i++) {
    if (workers[i] < 0 || workers[i] == current) {
      workerCancelled[i] = cancelled;
      workers[i] = current;
      return artworkSliceCheckpoint();
    }
  }
  return -1;
}

void artworkSliceJobEnd(void) {
  int current = GetThreadId();
  for (int i = 0; i < 2; i++) {
    if (workers[i] == current) {
      workers[i] = -1;
      workerCancelled[i] = NULL;
    }
  }
}

void artworkSliceCancelJob(volatile int *cancelled) {
  *cancelled = 1;
  // Cancellation must also reach workers sleeping at an exhausted frame budget.
  if (gate >= 0) {
    SignalSema(gate);
    SignalSema(gate);
  }
}

int artworkSliceCheckpoint(void) {
  if (workers[0] < 0 && workers[1] < 0) return 0;
  int current = GetThreadId();
  int slot = workers[0] == current ? 0 : workers[1] == current ? 1 : -1;
  if (slot < 0) return 0;
  const volatile int *cancelled = workerCancelled[slot];
  for (;;) {
    if (cancelled != NULL && *cancelled) return -1;
    if (!enabled || (int32_t)(nowUs() - deadline) < 0) return 0;
    if (WaitSema(gate) < 0) return 0;
  }
}
