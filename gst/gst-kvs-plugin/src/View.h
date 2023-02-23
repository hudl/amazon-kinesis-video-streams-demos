#ifndef __VIEW_H__
#define __VIEW_H__

#include <com/amazonaws/kinesis/video/common/CommonDefs.h>
#include <stdbool.h>

#define DEFAULT_CAMERA_NAME      "5c000cbc907d2fd20ca9a038"
#define UNINITIALIZED_CAMERA_IDX (-1)
#define DEFAULT_PAN              90
#define DEFAULT_TILT             (-30)
#define DEFAULT_ZOOM             2

#define DEFAULT_DEGREE_STEP 0.5
#define DEFAULT_PERCENTAGE  2.0

#define MIN_PAN  0
#define MAX_PAN  180
#define MIN_TILT (-50)
#define MAX_TILT 0
#define MIN_ZOOM 1.0
#define MAX_ZOOM 10

typedef struct CameraView {
    volatile ATOMIC_BOOL viewThreadStarted;
    volatile ATOMIC_BOOL interrupted;
    CVAR cvar;
    TID viewTid;
    MUTEX viewLock;
    int cameraIdx;
    double pan;
    double tilt;
    double zoom;
} CameraView, *PCameraView;

extern PCameraView gVirtcamView;

VOID setDefaultVirtcamView(PCameraView);
STATUS createVirtcamView(PCameraView*);
bool isViewValid(const CameraView);

#endif  // __VIEW_H__
