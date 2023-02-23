#include "View.h"

#include <com/amazonaws/kinesis/video/common/PlatformUtils.h>

VOID setDefaultVirtcamView(PCameraView pVirtcamView) {
    if (pVirtcamView != NULL) {
        pVirtcamView->pan = DEFAULT_PAN;
        pVirtcamView->tilt = DEFAULT_TILT;
        pVirtcamView->zoom = DEFAULT_ZOOM;
    }
}

STATUS createVirtcamView(PCameraView* ppVirtcamView) {
    PCameraView pVirtcamView = NULL;
    if (NULL == (pVirtcamView = (PCameraView) MEMCALLOC(1, SIZEOF(CameraView)))) {
        return STATUS_NOT_ENOUGH_MEMORY;
    }

    pVirtcamView->cameraIdx = UNINITIALIZED_CAMERA_IDX;
    setDefaultVirtcamView(pVirtcamView);
    pVirtcamView->viewLock = MUTEX_CREATE(TRUE);
    pVirtcamView->viewTid = INVALID_TID_VALUE;
    ATOMIC_STORE_BOOL(&pVirtcamView->viewThreadStarted, FALSE);
    ATOMIC_STORE_BOOL(&pVirtcamView->interrupted, FALSE);
    pVirtcamView->cvar = CVAR_CREATE();
    *ppVirtcamView = pVirtcamView;

    return STATUS_SUCCESS;
}

bool isViewValid(const CameraView view) {
    if (view.cameraIdx == UNINITIALIZED_CAMERA_IDX) {
        DLOGI("Invalid view (Id uninitialized)");
        return false;
    }

    if (view.pan > MAX_PAN || view.pan < MIN_PAN ||
        view.tilt > MAX_TILT || view.tilt < MIN_TILT ||
        view.zoom > MAX_ZOOM || view.zoom < MIN_ZOOM) {
        DLOGI("Invalid view (out of range)");
        return false;
    }
    return true;
}

STATUS freeVirtcamView(PCameraView pVirtcamView) {
    if (pVirtcamView != NULL) {
        if (pVirtcamView->viewTid != INVALID_TID_VALUE) {
            THREAD_JOIN(pVirtcamView->viewTid, NULL);
        }
        SAFE_MEMFREE(pVirtcamView);
    }
}
