#include "Keyboard.h"

#include <stdio.h>
#include <string.h>

#include "VirtcamCurl.h"

CameraView getNewViewByKey(const char* keyCode) {
    MUTEX_LOCK(gVirtcamView->viewLock);
    CameraView newView = *gVirtcamView;
    MUTEX_UNLOCK(gVirtcamView->viewLock);

    if (strcmp(keyCode, "ARROWLEFT") == 0) {
        newView.pan += DEFAULT_DEGREE_STEP;
    } else if (strcmp(keyCode, "ARROWRIGHT") == 0) {
        newView.pan -= DEFAULT_DEGREE_STEP;
    } else if (strcmp(keyCode, "ARROWUP") == 0) {
        newView.tilt += DEFAULT_DEGREE_STEP;
    } else if (strcmp(keyCode, "ARROWDOWN") == 0) {
        newView.tilt -= DEFAULT_DEGREE_STEP;
    } else if (strcmp(keyCode, "A") == 0) {
        double delta = newView.zoom * DEFAULT_PERCENTAGE / 100;
        newView.zoom += delta;
    } else if (strcmp(keyCode, "Z") == 0) {
        double delta = newView.zoom * DEFAULT_PERCENTAGE / 100;
        newView.zoom -= delta;
    } else {
        printf("%s:%d: Key: %s unsupported\n", __FUNCTION__, __LINE__, keyCode);
    }

    return newView;
}

/*
 * The `camera` is moved only for valid keys and if the move is in range, `view` is updated when moving is complete
 */
bool moveViewWithKey(const char* keyCode) {
    CameraView newView = getNewViewByKey(keyCode);
    if (curlMoveCamera(newView)) {
        MUTEX_LOCK(gVirtcamView->viewLock);
        *gVirtcamView = newView;
        MUTEX_UNLOCK(gVirtcamView->viewLock);
        return true;
    }

    return false;
}
