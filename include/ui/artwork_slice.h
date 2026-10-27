#ifndef LUNA_UI_ARTWORK_SLICE_H
#define LUNA_UI_ARTWORK_SLICE_H
void artworkSliceFrame(int collectionActive);
void artworkSliceGrant(void);
void artworkSliceDisable(void);
int artworkSliceJobBegin(int collectionJob, const volatile int *cancelled);
void artworkSliceJobEnd(void);
void artworkSliceCancelJob(volatile int *cancelled);
// Returns nonzero when this worker should release its partial artwork.
int artworkSliceCheckpoint(void);
#endif
